#include "config/config.h"
#include "os/fs.h"

CString config_ast_tag_to_cstr [] = {
    #define X(TAG, ...) #TAG,
        EACH_CONFIG_AST_NODE(X)
    #undef X
};

U64 config_ast_get_node_size [] = {
    #define X(_, T, ...) sizeof(T),
        EACH_CONFIG_AST_NODE(X)
    #undef X
};

U8 config_ast_get_node_align [] = {
    #define X(_, T, ...) alignof(T),
        EACH_CONFIG_AST_NODE(X)
    #undef X
};

static ConfigAst *parse_val (Config *cfg);
static ConfigAst *parse_keyval (Config *cfg);
static ConfigAst *parse_error (Config *cfg, CString msg);

static Void error (Config *cfg, SrcPos pos, CString msg, ...) {
    VaList va;
    va_start(va, msg);
    astr_push_fmt_va(&cfg->log, msg, va);
    va_end(va);
    astr_push_2u8(&cfg->log, '\n', '\n');
    tmem_new(tm);
    SrcLog *slog = slog_new(tm, slog_default_config);
    slog_add_src(slog, 0, cfg->path, cfg->text);
    slog_add_pos(slog, 0, pos);
    slog_flush(slog, &cfg->log);
    astr_push_u8(&cfg->log, '\n');
    astr_println(&cfg->log);
    panic();
}

static Void error_typematch (Config *cfg, ConfigAst *a, ConfigAst *b) {
    astr_push_cstr(&cfg->log, "Type mismatch:\n\n");
    tmem_new(tm);
    SrcLog *slog = slog_new(tm, slog_default_config);
    slog_add_src(slog, 0, cfg->path, cfg->text);
    slog_add_pos(slog, 0, a->pos);
    slog_add_pos(slog, 0, b->pos);
    slog_flush(slog, &cfg->log);
    astr_push_u8(&cfg->log, '\n');
    astr_println(&cfg->log);
    panic();
}

ConfigAst *config_ast_alloc (Mem *mem, ConfigAstTag tag) {
    ConfigAst *node = mem_alloc(mem, ConfigAst, .size=config_ast_get_node_size[tag], .align=config_ast_get_node_align[tag]);
    node->tag = tag;
    array_init(&node->children, mem);
    return node;
}

static Void on_token_eaten (Token *token, Void *p_) {
    Auto cfg = cast(Config*, p_);
    cfg->last_eaten_token = token->pos;
}

static ConfigAst *complete_node (Config *cfg, ConfigAst *node) {
    SrcPos prev = cfg->last_eaten_token;
    node->pos.last_line = prev.last_line;
    node->pos.length = prev.offset + prev.length - node->pos.offset;
    return node;
}

static ConfigAst *make_node (Config *cfg, ConfigAstTag tag) {
    ConfigAst *node = config_ast_alloc(cfg->mem, tag);
    SrcPos pos = lex_peek(cfg->lex)->pos;
    node->pos.offset = pos.offset;
    node->pos.first_line = pos.first_line;
    return node;
}

static ConfigAst *make_node_lhs (Config *cfg, ConfigAstTag tag, ConfigAst *lhs) {
    ConfigAst *node = config_ast_alloc(cfg->mem, tag);
    node->pos.offset = lhs->pos.offset;
    node->pos.first_line = lhs->pos.first_line;
    return node;
}

String config_ast_to_str (ConfigAst *node, String text) {
    return (String){
        .data  = text.data + node->pos.offset,
        .count = node->pos.length,
    };
}

static Bool match (Config *cfg, ConfigAst *a, ConfigAst *b) {
    if (a->tag != b->tag) {
        error_typematch(cfg, a, b);
        return false;
    }

    if (a->tag == CONFIG_AST_ARRAY) {
        if (a->children.count != b->children.count) {
            error_typematch(cfg, a, b);
            return false;
        }

        array_iter (x, &a->children) {
            ConfigAst *y = array_get(&b->children, ARRAY_IDX);
            match(cfg, x, y);
        }
    }

    if (a->tag == CONFIG_AST_STRUCT) {
        if (a->children.count != b->children.count) {
            error_typematch(cfg, a, b);
            return false;
        }

        array_iter (f1, &a->children) {
            ConfigAst *f2 = array_get(&b->children, ARRAY_IDX);

            if (! str_match(cast(ConfigAstKeyval*, f1)->key, cast(ConfigAstKeyval*, f2)->key)) {
                error_typematch(cfg, f1, f2);
                return false;
            }

            match(cfg, array_get(&f1->children, 0), array_get(&f2->children, 0));
        }
    }

    return true;
}

static ConfigAst *parse_bool (Config *cfg) {
    ConfigAst *node = make_node(cfg, CONFIG_AST_BOOL);
    String text = lex_eat(cfg->lex)->text;
    cast(ConfigAstBool*, node)->value = str_match(text, str("true"));
    return complete_node(cfg, node);
}

static ConfigAst *parse_array (Config *cfg) {
    ConfigAst *node = make_node(cfg, CONFIG_AST_ARRAY);
    lex_eat(cfg->lex);

    while (true) {
        Token *token = lex_peek(cfg->lex);

        if (token->tag == ']' || token->tag == TOKEN_EOF) {
            lex_eat(cfg->lex);
            break;
        }

        ConfigAst *value = parse_val(cfg);
        array_push(&node->children, value);
    }

    ConfigAst *first_child = array_try_get(&node->children, 0);
    array_iter_from (child, &node->children, 1) {
        if (! match(cfg, first_child, child)) {
            node->tag = CONFIG_AST_ERROR;
            break;
        }
    }

    return complete_node(cfg, node);
}

static ConfigAst *parse_string (Config *cfg) {
    ConfigAst *node = make_node(cfg, CONFIG_AST_STRING);
    lex_eat(cfg->lex);

    while (true) {
        Token *token = lex_peek(cfg->lex);

        switch (token->tag) {
        case TOKEN_EOF: break;
        case '\\': lex_eat(cfg->lex); lex_eat(cfg->lex); break;
        case '"': goto brk;
        default: lex_eat(cfg->lex); break;
        }
    } brk:;

    lex_eat(cfg->lex);
    complete_node(cfg, node);

    String escaped = config_ast_to_str(node, cfg->text);
    String unescaped = str_unescape(cfg->mem, escaped);
    cast(ConfigAstString*, node)->value = unescaped;

    return node;
}

static ConfigAst *parse_int (Config *cfg) {
    ConfigAst *node = make_node(cfg, CONFIG_AST_U64);
    Token *token = lex_eat(cfg->lex);

    if (token->valid_number) {
        cast(ConfigAstU64*, node)->value = token->u64;
    } else {
        error(cfg, token->pos, "Invalid U64 number.");
        node->tag = CONFIG_AST_ERROR;
    }

    return complete_node(cfg, node);
}

static ConfigAst *parse_float (Config *cfg) {
    ConfigAst *node = make_node(cfg, CONFIG_AST_F64);
    Token *token = lex_eat(cfg->lex);

    if (token->valid_number) {
        cast(ConfigAstF64*, node)->value = token->f64;
    } else {
        error(cfg, token->pos, "Invalid F64 number.");
        node->tag = CONFIG_AST_ERROR;
    }

    return complete_node(cfg, node);
}

static ConfigAst *parse_negate (Config *cfg) {
    if (! lex_try_peek_nth(cfg->lex, 2, TOKEN_U64)) {
        return parse_error(cfg, "Invalid operand to negation operator.");
    }

    lex_eat(cfg->lex);

    ConfigAst *node = parse_int(cfg);

    if (node->tag == CONFIG_AST_U64) {
        U64 value = cast(ConfigAstU64*, node)->value;
        if (value > cast(U64, INT64_MAX) + 1) {
            error(cfg, node->pos, "Invalid negative int.");
            node->tag = CONFIG_AST_ERROR;
        }
        node->tag = CONFIG_AST_I64;
        cast(ConfigAstI64*, node)->value = -cast(I64, value);
    }

    return node;
}

static ConfigAst *parse_struct (Config *cfg) {
    ConfigAst *node = make_node(cfg, CONFIG_AST_STRUCT);

    lex_eat(cfg->lex);

    while (true) {
        Token *token = lex_peek(cfg->lex);

        if (token->tag == '}' || token->tag == TOKEN_EOF) {
            lex_eat(cfg->lex);
            break;
        }

        ConfigAst *field = parse_keyval(cfg);
        array_push(&node->children, field);
    }

    return complete_node(cfg, node);
}

static ConfigAst *parse_val (Config *cfg) {
    Token *token = lex_peek(cfg->lex);

    switch (token->tag) {
    case TOKEN_WORD: {
        if (str_match(token->text, str("true")) || str_match(token->text, str("false"))) {
            return parse_bool(cfg);
        } else {
            return parse_error(cfg, "Expected value.");
        }
    }
    case TOKEN_U64: return parse_int(cfg);
    case TOKEN_F64: return parse_float(cfg);
    case '-': return parse_negate(cfg);
    case '[': return parse_array(cfg);
    case '{': return parse_struct(cfg);
    case '"': return parse_string(cfg);
    default: return parse_error(cfg, "Expected value.");
    }
}

static ConfigAst *parse_keyval (Config *cfg) {
    ConfigAst *node = make_node(cfg, CONFIG_AST_KEYVAL);
    Auto n = cast(ConfigAstKeyval*, node);

    n->key = lex_eat(cfg->lex)->text;

    Token *token = lex_peek(cfg->lex);
    if (token->tag != '=') {
        error(cfg, token->pos, "Expected '=' token:");
    } else {
        lex_eat(cfg->lex);
    }

    array_push(&node->children, parse_val(cfg));
    return complete_node(cfg, node);
}

static ConfigAst *parse_error (Config *cfg, CString msg) {
    ConfigAst *node = make_node(cfg, CONFIG_AST_ERROR);
    SrcPos pos = lex_peek(cfg->lex)->pos;
    lex_eat_line(cfg->lex);
    complete_node(cfg, node);
    error(cfg, pos, msg);
    return complete_node(cfg, node);
}

static ConfigAst *parse_root (Config *cfg) {
    ConfigAst *node = make_node(cfg, CONFIG_AST_ROOT);

    while (true) {
        Token *token = lex_peek(cfg->lex);

        switch (token->tag) {
        case '#':        lex_eat_line(cfg->lex); break;
        case TOKEN_EOF:  goto brk;
        case TOKEN_WORD: array_push(&node->children, parse_keyval(cfg)); break;
        default:         array_push(&node->children, parse_error(cfg, "Expected key name.")); break;
        }
    } brk:;

    return complete_node(cfg, node);
}

Config *config_parse (Mem *mem, String path) {
    String file = fs_read_entire_file(mem, path, 0);
    Auto cfg = mem_new(mem, Config);
    cfg->mem = mem;
    cfg->text = file;
    cfg->log = astr_new(mem);
    cfg->path = path;
    cfg->lex = lex_new(mem, true);
    lex_reset(cfg->lex, file);
    lex_set_on_token_eaten(cfg->lex, on_token_eaten, cfg);
    cfg->root = parse_root(cfg);
    if (! file.data) {
        astr_push_fmt(&cfg->log, "Could not read_file: " TERM_CYAN("[%.*s]"), STR(path));
        astr_println(&cfg->log);
        panic();
    }
    return cfg;
}

static ConfigAst *get_val (Config *cfg, ConfigAst *node, CString key) {
    if (node->tag != CONFIG_AST_ROOT && node->tag != CONFIG_AST_STRUCT) {
        error(cfg, node->pos, "Attempting a field lookup in something that is not a struct.");
        return 0;
    }

    String key_str = str(key);
    array_iter (keyval, &node->children) {
        if (keyval->tag != CONFIG_AST_KEYVAL) continue;
        String key = cast(ConfigAstKeyval*, keyval)->key;
        if (str_match(key, key_str)) return array_get(&keyval->children, 0);
    }

    error(cfg, node->pos, "Could not find key [%s].", key);
    return 0;
}

static Bool verify_val_type (Config *cfg, ConfigAst *val, ConfigAstTag tag) {
    if (! val) {
        return false;
    } else if (val->tag != tag) {
        error(cfg, val->pos, "Bad type.");
        return false;
    } else {
        return true;
    }
}

Bool config_get_bool (Config *cfg, ConfigAst *node, CString key) {
    ConfigAst *val = get_val(cfg, node, key);
    return verify_val_type(cfg, val, CONFIG_AST_BOOL) ? cast(ConfigAstBool*, val)->value : 0;
}

I64 config_get_i64 (Config *cfg, ConfigAst *node, CString key) {
    ConfigAst *val = get_val(cfg, node, key);
    return verify_val_type(cfg, val, CONFIG_AST_I64) ? cast(ConfigAstI64*, val)->value : 0;
}

U64 config_get_u64 (Config *cfg, ConfigAst *node, CString key) {
    ConfigAst *val = get_val(cfg, node, key);
    return verify_val_type(cfg, val, CONFIG_AST_U64) ? cast(ConfigAstU64*, val)->value : 0;
}

F64 config_get_f64 (Config *cfg, ConfigAst *node, CString key) {
    ConfigAst *val = get_val(cfg, node, key);
    return verify_val_type(cfg, val, CONFIG_AST_F64) ? cast(ConfigAstF64*, val)->value : 0;
}

Vec2 config_get_vec2 (Config *cfg, ConfigAst *node, CString key) {
    ConfigAst *val = get_val(cfg, node, key);
    verify_val_type(cfg, val, CONFIG_AST_ARRAY);
    if (val->children.count != 2) error(cfg, node->pos, "Expected array of length 2.");
    verify_val_type(cfg, array_get(&val->children, 0), CONFIG_AST_F64);
    Vec2 result;
    array_iter (c, &val->children) result.v[ARRAY_IDX] = cast(ConfigAstF64*, c)->value;
    return result;
}

Vec4 config_get_vec4 (Config *cfg, ConfigAst *node, CString key) {
    ConfigAst *val = get_val(cfg, node, key);
    verify_val_type(cfg, val, CONFIG_AST_ARRAY);
    if (val->children.count != 4) error(cfg, node->pos, "Expected array of length 4.");
    verify_val_type(cfg, array_get(&val->children, 0), CONFIG_AST_F64);
    Vec4 result;
    array_iter (c, &val->children) result.v[ARRAY_IDX] = cast(ConfigAstF64*, c)->value;
    return result;
}

String config_get_string (Config *cfg, ConfigAst *node, CString key, Mem *mem) {
    ConfigAst *val = get_val(cfg, node, key);
    String result = verify_val_type(cfg, val, CONFIG_AST_STRING) ? cast(ConfigAstString*, val)->value : (String){};
    return mem ? str_copy(mem, result) : result;
}

ConfigAst *config_get_array  (Config *cfg, ConfigAst *node, CString key) {
    ConfigAst *val = get_val(cfg, node, key);
    return verify_val_type(cfg, val, CONFIG_AST_ARRAY) ? val : 0;
}

ConfigAst *config_get_struct (Config *cfg, ConfigAst *node, CString key) {
    ConfigAst *val = get_val(cfg, node, key);
    return verify_val_type(cfg, val, CONFIG_AST_STRUCT) ? val : 0;
}
