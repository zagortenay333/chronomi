#include "core/config.h"
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

static ConfigAst *parse_val (ConfigParser *p);
static ConfigAst *parse_keyval (ConfigParser *p);
static ConfigAst *parse_error (ConfigParser *p, CString msg);

static Void error (ConfigParser *p, SrcPos pos, CString msg, ...) {
    if (! p->log) return;
    VaList va;
    va_start(va, msg);
    astr_push_fmt_va(p->log, msg, va);
    va_end(va);
    astr_push_2u8(p->log, '\n', '\n');
    tmem_new(tm);
    SrcLog *slog = slog_new(tm, &slog_default_config);
    slog_add_src(slog, 0, p->path, p->text);
    slog_add_pos(slog, 0, pos);
    slog_flush(slog, p->log);
    astr_push_u8(p->log, '\n');
}

static Void error_typematch (ConfigParser *p, ConfigAst *a, ConfigAst *b) {
    if (! p->log) return;
    print_stack_trace();
    astr_push_cstr(p->log, "Type mismatch:\n\n");
    tmem_new(tm);
    SrcLog *slog = slog_new(tm, &slog_default_config);
    slog_add_src(slog, 0, p->path, p->text);
    slog_add_pos(slog, 0, a->pos);
    slog_add_pos(slog, 0, b->pos);
    slog_flush(slog, p->log);
    astr_push_u8(p->log, '\n');
}

ConfigAst *config_ast_alloc (Mem *mem, ConfigAstTag tag) {
    ConfigAst *node = mem_alloc(mem, ConfigAst, .size=config_ast_get_node_size[tag], .align=config_ast_get_node_align[tag]);
    node->tag = tag;
    array_init(&node->children, mem);
    return node;
}

static Void on_token_eaten (Token *token, Void *p_) {
    Auto p = static_cast<ConfigParser *>(p_);
    p->last_eaten_token = token->pos;
}

static ConfigAst *complete_node (ConfigParser *p, ConfigAst *node) {
    SrcPos prev = p->last_eaten_token;
    node->pos.last_line = prev.last_line;
    node->pos.length = prev.offset + prev.length - node->pos.offset;
    return node;
}

static ConfigAst *make_node (ConfigParser *p, ConfigAstTag tag) {
    ConfigAst *node = config_ast_alloc(p->mem, tag);
    SrcPos pos = lex_peek(p->lex)->pos;
    node->pos.offset = pos.offset;
    node->pos.first_line = pos.first_line;
    return node;
}

static ConfigAst *make_node_lhs (ConfigParser *p, ConfigAstTag tag, ConfigAst *lhs) {
    ConfigAst *node = config_ast_alloc(p->mem, tag);
    node->pos.offset = lhs->pos.offset;
    node->pos.first_line = lhs->pos.first_line;
    return node;
}

String config_ast_to_str (ConfigAst *node, String text) {
    return {
        .data  = text.data + node->pos.offset,
        .count = node->pos.length,
    };
}

static Bool typematch (ConfigParser *p, ConfigAst *a, ConfigAst *b) {
    if (a->tag != b->tag) {
        error_typematch(p, a, b);
        return false;
    }

    if (a->tag == CONFIG_AST_ARRAY) {
        if (a->children.count != b->children.count) {
            error_typematch(p, a, b);
            return false;
        }

        array_iter (x, &a->children) {
            ConfigAst *y = array_get(&b->children, ARRAY_IDX);
            typematch(p, x, y);
        }
    }

    if (a->tag == CONFIG_AST_STRUCT) {
        if (a->children.count != b->children.count) {
            error_typematch(p, a, b);
            return false;
        }

        array_iter (f1, &a->children) {
            ConfigAst *f2 = array_get(&b->children, ARRAY_IDX);

            if (! str_match(reinterpret_cast<ConfigAstKeyval*>(f1)->key, reinterpret_cast<ConfigAstKeyval*>(f2)->key)) {
                error_typematch(p, f1, f2);
                return false;
            }

            typematch(p, array_get(&f1->children, 0), array_get(&f2->children, 0));
        }
    }

    return true;
}

static ConfigAst *parse_bool (ConfigParser *p) {
    ConfigAst *node = make_node(p, CONFIG_AST_BOOL);
    String text = lex_eat(p->lex)->text;
    reinterpret_cast<ConfigAstBool*>(node)->value = str_match(text, str("true"));
    return complete_node(p, node);
}

static ConfigAst *parse_array (ConfigParser *p) {
    ConfigAst *node = make_node(p, CONFIG_AST_ARRAY);
    lex_eat(p->lex);

    while (true) {
        Token *token = lex_peek(p->lex);

        if (token->tag == ']' || token->tag == TOKEN_EOF) {
            lex_eat(p->lex);
            break;
        }

        ConfigAst *value = parse_val(p);
        array_push(&node->children, value);
    }

    ConfigAst *first_child = array_try_get(&node->children, 0);
    array_iter_from (child, &node->children, 1) {
        if (! typematch(p, first_child, child)) {
            node->tag = CONFIG_AST_ERROR;
            break;
        }
    }

    return complete_node(p, node);
}

static ConfigAst *parse_string (ConfigParser *p) {
    ConfigAst *node = make_node(p, CONFIG_AST_STRING);
    lex_eat(p->lex);

    while (true) {
        Token *token = lex_peek(p->lex);

        switch (token->tag) {
        case TOKEN_EOF: break;
        case '\\': lex_eat(p->lex); lex_eat(p->lex); break;
        case '"': goto brk;
        default: lex_eat(p->lex); break;
        }
    } brk:;

    lex_eat(p->lex);
    complete_node(p, node);

    String escaped = config_ast_to_str(node, p->text);
    String unescaped = str_unescape(p->mem, escaped);
    reinterpret_cast<ConfigAstString*>(node)->value = unescaped;

    return node;
}

static ConfigAst *parse_int (ConfigParser *p) {
    ConfigAst *node = make_node(p, CONFIG_AST_U64);
    Token *token = lex_eat(p->lex);

    if (token->valid_number) {
        reinterpret_cast<ConfigAstU64*>(node)->value = token->u64;
    } else {
        error(p, token->pos, "Invalid U64 number.");
        node->tag = CONFIG_AST_ERROR;
    }

    return complete_node(p, node);
}

static ConfigAst *parse_float (ConfigParser *p) {
    ConfigAst *node = make_node(p, CONFIG_AST_F64);
    Token *token = lex_eat(p->lex);

    if (token->valid_number) {
        reinterpret_cast<ConfigAstF64*>(node)->value = token->f64;
    } else {
        error(p, token->pos, "Invalid F64 number.");
        node->tag = CONFIG_AST_ERROR;
    }

    return complete_node(p, node);
}

static ConfigAst *parse_negate (ConfigParser *p) {
    if (! lex_try_peek_nth(p->lex, 2, TOKEN_U64)) {
        return parse_error(p, "Invalid operand to negation operator.");
    }

    lex_eat(p->lex);

    ConfigAst *node = parse_int(p);

    if (node->tag == CONFIG_AST_U64) {
        U64 value = reinterpret_cast<ConfigAstU64*>(node)->value;
        if (value > static_cast<U64>(INT64_MAX) + 1) {
            error(p, node->pos, "Invalid negative int.");
            node->tag = CONFIG_AST_ERROR;
        }
        node->tag = CONFIG_AST_I64;
        reinterpret_cast<ConfigAstI64*>(node)->value = -static_cast<I64>(value);
    }

    return node;
}

static ConfigAst *parse_struct (ConfigParser *p) {
    ConfigAst *node = make_node(p, CONFIG_AST_STRUCT);

    lex_eat(p->lex);

    while (true) {
        Token *token = lex_peek(p->lex);

        if (token->tag == '}' || token->tag == TOKEN_EOF) {
            lex_eat(p->lex);
            break;
        }

        ConfigAst *field = parse_keyval(p);
        array_push(&node->children, field);
    }

    return complete_node(p, node);
}

static ConfigAst *parse_val (ConfigParser *p) {
    Token *token = lex_peek(p->lex);

    switch (token->tag) {
    case TOKEN_WORD: {
        if (str_match(token->text, str("true")) || str_match(token->text, str("false"))) {
            return parse_bool(p);
        } else {
            return parse_error(p, "Expected value.");
        }
    }
    case TOKEN_U64: return parse_int(p);
    case TOKEN_F64: return parse_float(p);
    case '-': return parse_negate(p);
    case '[': return parse_array(p);
    case '{': return parse_struct(p);
    case '"': return parse_string(p);
    default: return parse_error(p, "Expected value.");
    }
}

static ConfigAst *parse_keyval (ConfigParser *p) {
    ConfigAst *node = make_node(p, CONFIG_AST_KEYVAL);
    Auto n = reinterpret_cast<ConfigAstKeyval*>(node);

    n->key = lex_eat(p->lex)->text;

    Token *token = lex_peek(p->lex);
    if (token->tag != '=') {
        error(p, token->pos, "Expected '=' token:");
    } else {
        lex_eat(p->lex);
    }

    array_push(&node->children, parse_val(p));
    return complete_node(p, node);
}

static ConfigAst *parse_error (ConfigParser *p, CString msg) {
    ConfigAst *node = make_node(p, CONFIG_AST_ERROR);
    SrcPos pos = lex_peek(p->lex)->pos;
    lex_eat_line(p->lex);
    complete_node(p, node);
    error(p, pos, msg);
    return complete_node(p, node);
}

static ConfigAst *parse_root (ConfigParser *p) {
    ConfigAst *node = make_node(p, CONFIG_AST_ROOT);

    while (true) {
        Token *token = lex_peek(p->lex);

        switch (token->tag) {
        case '#':        lex_eat_line(p->lex); break;
        case TOKEN_EOF:  goto brk;
        case TOKEN_WORD: array_push(&node->children, parse_keyval(p)); break;
        default:         array_push(&node->children, parse_error(p, "Expected key name.")); break;
        }
    } brk:;

    return complete_node(p, node);
}

ConfigParser *config_load (Mem *mem, String path, AString *log) {
    String file = fs_read_entire_file(mem, path, 0);
    Auto p = mem_new(mem, ConfigParser);
    p->mem = mem;
    p->text = file;
    p->log = log;
    p->path = path;
    p->lex = lex_new(mem, true);
    lex_reset(p->lex, file);
    lex_set_on_token_eaten(p->lex, on_token_eaten, p);
    p->root = parse_root(p);
    return p;
}

static ConfigAst *get_val (ConfigParser *p, ConfigAst *node, CString key, Bool *out_error) {
    if (node->tag != CONFIG_AST_ROOT && node->tag != CONFIG_AST_STRUCT) {
        *out_error = true;
        return 0;
    }

    String key_str = str(key);
    array_iter (keyval, &node->children) {
        if (keyval->tag != CONFIG_AST_KEYVAL) continue;
        String key = reinterpret_cast<ConfigAstKeyval*>(keyval)->key;
        if (str_match(key, key_str)) return array_get(&keyval->children, 0);
    }

    *out_error = true;
    error(p, node->pos, "Could not find key [%s].", key);
    return 0;
}

static Bool verify_val_type (ConfigParser *p, ConfigAst *val, ConfigAstTag tag, Bool *out_error) {
    if (! val) {
        return false;
    } else if (val->tag != tag) {
        *out_error = true;
        error(p, val->pos, "Bad type.");
        return false;
    } else {
        return true;
    }
}

Bool config_get_bool (ConfigParser *p, ConfigAst *node, CString key, Bool *out_error) {
    ConfigAst *val = get_val(p, node, key, out_error);
    return verify_val_type(p, val, CONFIG_AST_BOOL, out_error) ? reinterpret_cast<ConfigAstBool*>(val)->value : 0;
}

I64 config_get_i64 (ConfigParser *p, ConfigAst *node, CString key, Bool *out_error) {
    ConfigAst *val = get_val(p, node, key, out_error);
    return verify_val_type(p, val, CONFIG_AST_I64, out_error) ? reinterpret_cast<ConfigAstI64*>(val)->value : 0;
}

U64 config_get_u64 (ConfigParser *p, ConfigAst *node, CString key, Bool *out_error) {
    ConfigAst *val = get_val(p, node, key, out_error);
    return verify_val_type(p, val, CONFIG_AST_U64, out_error) ? reinterpret_cast<ConfigAstU64*>(val)->value : 0;
}

F64 config_get_f64 (ConfigParser *p, ConfigAst *node, CString key, Bool *out_error) {
    ConfigAst *val = get_val(p, node, key, out_error);
    return verify_val_type(p, val, CONFIG_AST_F64, out_error) ? reinterpret_cast<ConfigAstF64*>(val)->value : 0;
}

String config_get_string (ConfigParser *p, ConfigAst *node, CString key, Bool *out_error) {
    ConfigAst *val = get_val(p, node, key, out_error);
    return verify_val_type(p, val, CONFIG_AST_STRING, out_error) ? reinterpret_cast<ConfigAstString*>(val)->value : String{};
}

ConfigAst *config_get_array  (ConfigParser *p, ConfigAst *node, CString key, Bool *out_error) {
    ConfigAst *val = get_val(p, node, key, out_error);
    return verify_val_type(p, val, CONFIG_AST_ARRAY, out_error) ? val : 0;
}

ConfigAst *config_get_struct (ConfigParser *p, ConfigAst *node, CString key, Bool *out_error) {
    ConfigAst *val = get_val(p, node, key, out_error);
    return verify_val_type(p, val, CONFIG_AST_STRUCT, out_error) ? val : 0;
}
