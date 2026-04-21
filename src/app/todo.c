#include "app/app.h"
#include "ui/ui_widgets.h"
#include "ui/ui_markup_view.h"
#include "ui/ui_text_editor.h"
#include "config/config.h"
#include "buffer/buffer.h"
#include "app/todo.h"
#include "os/time.h"

ienum (SortBy, U8) {
    SORT_BY_PIN,
    SORT_BY_DUE,
    SORT_BY_PRIORITY,
    SORT_BY_DONE,
    SORT_BY_HIDE,
    SORT_COUNT
};

istruct (Sort) {
    Bool ascending;
    SortBy by;
};

istruct (Deck) {
    Bool active;
    Buf *path;
    Buf *filters;
};

istruct (Task) {
    String text; // Superset of the task text. Use markup_ast_get_text().
    MarkupAst *ast;
    MarkupAstMetaConfig *config;
};

array_typedef(Sort, Sort);
array_typedef(Deck, Deck);
array_typedef(Task, Task);

istruct (KanbanColumn) {
    Bool with_header;
    MarkupAst *filter_node;
    String filter_text;
    ArrayU64 tasks;
    U64 show_more_idx;
};

istruct (SearchResult) {
    I64 score;
    U64 idx;
};

ienum (ViewTag, U8) {
    VIEW_KANBAN,
    VIEW_SEARCH,
    VIEW_EDITOR,
    VIEW_SORT,
    VIEW_DECK_BROWSER,
};

istruct (View) {
    ViewTag tag;

    union {
        struct {
            Bool has_filters;
            ArrayString filters;
            Array(KanbanColumn*) columns;
        } kanban;

        struct {
            Bool dragging;
            U64 draggee;
            U64 show_more;
        } sort;

        struct {
            Buf *buf;
            U64 cursor;
            U64 task_idx;
            Task task;
        } editor;

        struct {
            Buf *buf;
            U64 buf_version;
            U64 show_more_idx;
            Bool delete_searched;
            ArrayU64 searched;
        } search;

        struct {
            Buf *buf;
            Bool delete_searched;
            Array(SearchResult) searched;
        } deck_browser;
    };
};

ienum (CommandTag, U8) {
    CMD_SAVE_CONFIG,
    CMD_NEW_TASK_FLAGS,
    CMD_ADD_TASKS,
    CMD_DEL_TASK,
    CMD_NEW_DECK,
    CMD_DEL_DECK,
    CMD_ACTIVATE_DECK,
    CMD_CHANGE_SORT,
    CMD_VIEW_KANBAN,
    CMD_VIEW_EDITOR,
    CMD_VIEW_SORT,
    CMD_VIEW_SEARCH,
    CMD_VIEW_DECK_BROWSER,
};

istruct (Command) {
    CommandTag tag;
    Bool skip_config_save;
    Bool save_deck;
    MarkupAstMetaConfigFlags flags;
    U64 idx;
    U64 idx2;
    String str;
};

istruct (Context) {
    View view;
    Mem *view_mem;
    U64 config_version;
    String config_file_path;
    Array(Command) commands;
    Mem *config_mem;
    U64 config_mem_fragmentation;
    ArraySort sorts;
    ArrayDeck decks;
    ArrayTask tasks;
    ArrayString non_tasks;
    F32 editor_width;
    F32 editor_height;
};

static Context *context;

#define push_command(...) array_push_lit(&context->commands, __VA_ARGS__)

String sort_to_string (SortBy sort) {
    switch (sort) {
    case SORT_BY_PIN:      return str("Sort by pin");
    case SORT_BY_DUE:      return str("Sort by due");
    case SORT_BY_PRIORITY: return str("Sort by priority");
    case SORT_BY_DONE:     return str("Sort by done");
    case SORT_BY_HIDE:     return str("Sort by hide");
    default:               return str("");
    }
}

I64 compare_tasks (Task *a, Task *b) {
    array_iter (sort, &context->sorts) {
        I64 result = 0;

        switch (sort.by) {
        case SORT_BY_PIN: {
            I64 x  = !!(a->config->flags & MARKUP_AST_META_CONFIG_HAS_PIN);
            I64 y  = !!(b->config->flags & MARKUP_AST_META_CONFIG_HAS_PIN);
            result = x - y;
        } break;
        case SORT_BY_DONE: {
            I64 x  = !!(a->config->flags & MARKUP_AST_META_CONFIG_HAS_DONE);
            I64 y  = !!(b->config->flags & MARKUP_AST_META_CONFIG_HAS_DONE);
            result = x - y;
        } break;
        case SORT_BY_HIDE: {
            I64 x  = !!(a->config->flags & MARKUP_AST_META_CONFIG_HAS_HIDE);
            I64 y  = !!(b->config->flags & MARKUP_AST_META_CONFIG_HAS_HIDE);
            result = x - y;
        } break;
        case SORT_BY_DUE: {
            String x = (a->config->flags & MARKUP_AST_META_CONFIG_HAS_DUE) ? a->config->due : str("9999-99-99");
            String y = (b->config->flags & MARKUP_AST_META_CONFIG_HAS_DUE) ? b->config->due : str("9999-99-99");
            array_iter (X, &x) {
                Char Y = array_get(&y, ARRAY_IDX);
                result = (X < Y) ? -1 : (X > Y) ? 1 : 0;
                if (result) break;
            }
        } break;
        case SORT_BY_PRIORITY: {
            U64 x  = (a->config->flags & MARKUP_AST_META_CONFIG_HAS_PRIORITY) ? a->config->priority : UINT64_MAX;
            U64 y  = (b->config->flags & MARKUP_AST_META_CONFIG_HAS_PRIORITY) ? b->config->priority : UINT64_MAX;
            result = (x < y) ? -1 : (x > y) ? 1 : 0;
        } break;
        default: break;
        }

        if (result) return sort.ascending ? result : -result;
    }

    return 0;
}

static Int cmp_tasks_ (Void *a, Void *b) {
    return compare_tasks(a, b);
}

static Void sort_tasks () {
    array_sort_cmp(&context->tasks, cmp_tasks_);
}

static Bool task_passes_filter_ (Task *task, MarkupAst *filter) {
    MarkupAstMetaConfig *c = task->config;
    String text = markup_ast_get_text(task->ast, task->text);
    MarkupAst *op1 = array_try_get(&filter->children, 0);
    MarkupAst *op2 = array_try_get(&filter->children, 1);

    switch (filter->tag) {
    case MARKUP_AST_FILTER_NOT:      return !task_passes_filter_(task, op1);
    case MARKUP_AST_FILTER_OR:       return task_passes_filter_(task, op1) || task_passes_filter_(task, op2);
    case MARKUP_AST_FILTER_AND:      return task_passes_filter_(task, op1) && task_passes_filter_(task, op2);
    case MARKUP_AST_FILTER_ANY:      return true;
    case MARKUP_AST_FILTER_DUE:      return c->flags & MARKUP_AST_META_CONFIG_HAS_DUE;
    case MARKUP_AST_FILTER_DONE:     return c->flags & MARKUP_AST_META_CONFIG_HAS_DONE;
    case MARKUP_AST_FILTER_PIN:      return c->flags & MARKUP_AST_META_CONFIG_HAS_PIN;
    case MARKUP_AST_FILTER_HIDE:     return c->flags & MARKUP_AST_META_CONFIG_HAS_HIDE;
    case MARKUP_AST_FILTER_TAG:      return (c->flags & MARKUP_AST_META_CONFIG_HAS_TAGS) && map_has(&c->tags, cast(MarkupAstFilterTag*, filter)->text);
    case MARKUP_AST_FILTER_TRACK:    return (c->flags & MARKUP_AST_META_CONFIG_HAS_TRACK) && (cast(MarkupAstFilterTrack*, filter)->id == c->track);
    case MARKUP_AST_FILTER_FUZZY:    return str_fuzzy_search(cast(MarkupAstFilterFuzzy*, filter)->needle, text, 0) != INT64_MIN;
    case MARKUP_AST_FILTER_STRING:   return str_search(cast(MarkupAstFilterString*, filter)->needle, text) != ARRAY_NIL_IDX;
    case MARKUP_AST_FILTER_PRIORITY: return (c->flags & MARKUP_AST_META_CONFIG_HAS_PRIORITY) && (cast(MarkupAstFilterPriority*, filter)->priority == c->priority);
    case MARKUP_AST_FILTER_ERROR:    return false;
    default: badpath;
    }
}

static Bool task_passes_filter (Task *task, MarkupAst *filter) {
    MarkupAst *op1 = array_try_get(&filter->children, 0);

    if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_HIDE) {
        if (filter->tag == MARKUP_AST_FILTER_ANY || filter->tag == MARKUP_AST_FILTER_HIDE) return true;
        if (! (filter->tag == MARKUP_AST_FILTER_AND && op1->tag == MARKUP_AST_FILTER_HIDE)) return false;
    }

    return task_passes_filter_(task, filter);
}

static Void task_serialize (U64 idx) {
    Task *task = array_ref(&context->tasks, idx);
    AString a = astr_new(context->config_mem);

    astr_push_byte(&a, '[');
    if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_DONE)      astr_push_cstr(&a, "x ");
    if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_PRIORITY)  astr_push_fmt(&a, "#%lu ", task->config->priority);
    if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_TRACK)     astr_push_fmt(&a, "track:%lu ", task->config->track);
    if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_CREATED)   astr_push_fmt(&a, "created:%.*s ", STR(task->config->created));
    if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_COMPLETED) astr_push_fmt(&a, "completed:%.*s ", STR(task->config->completed));
    if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_DUE)       astr_push_fmt(&a, "due:%.*s ", STR(task->config->due));
    if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_PIN)       astr_push_cstr(&a, "pin ");
    if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_HIDE)      astr_push_cstr(&a, "hide ");
    if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_TAGS)      map_iter (s, &task->config->tags) astr_push_fmt(&a, "%.*s ", STR(s->key));
    if (array_get_last(&a) == ' ') a.count--;
    astr_push_byte(&a, ']');

    MarkupAst *first_child = array_get(&task->ast->children, 0);
    MarkupAst *last_child  = array_get_last(&task->ast->children);
    U64 start   = first_child->pos.offset;
    U64 end     = last_child->pos.offset + last_child->pos.length;
    String body = str_slice(task->text, start, end-start);
    astr_push_byte(&a, ' ');
    astr_push_str(&a, body);

    task->text = astr_to_str(&a);
    MarkupAst *root = markup_parse(context->config_mem, task->text);
    task->ast = array_get(&root->children, 0);
    assert_always(task->ast->tag == MARKUP_AST_META);
    task->config = cast(MarkupAstMeta*, task->ast)->config;

    context->config_mem_fragmentation++;
}

static Bool is_valid_task (MarkupAst *node) {
    if (node->tag != MARKUP_AST_META) return false;
    Auto n = cast(MarkupAstMeta*, node);
    return !(n->config->flags & ~MARKUP_AST_META_TODO_FLAGS);
}

static Void add_task (String text, MarkupAst *ast) {
    assert_always(ast->tag == MARKUP_AST_META);
    Task *task = array_push_slot(&context->tasks);
    *task = (Task){
        .text   = text,
        .ast    = ast,
        .config = cast(MarkupAstMeta*, ast)->config,
    };
}

static Void add_tasks (String text, Bool copy_text) {
    if (copy_text) text = str_copy(context->config_mem, text);

    MarkupAst *root = markup_parse(context->config_mem, text);

    array_iter (node, &root->children) {
        if (is_valid_task(node)) {
            add_task(text, node);
        } else {
            String text = markup_ast_get_text(node, text);
            array_push(&context->non_tasks, text);
        }
    }
}

static Deck *get_active_deck () {
    array_iter (deck, &context->decks, *) if (deck->active) return deck;
    return array_try_ref(&context->decks, 0);
}

static Void save_active_deck () {
    tmem_new(tm);

    AString content = astr_new(tm);

    array_iter (task, &context->tasks, *) {
        String text = markup_ast_get_text(task->ast, task->text);
        astr_push_str(&content, text);
    }

    array_iter (non_task, &context->non_tasks) {
        astr_push_str(&content, non_task);
    }

    Deck *deck = get_active_deck();
    if (deck) fs_write_entire_file(buf_get_str(deck->path, tm), astr_to_str(&content));
}

static Void load_active_deck () {
    Deck *deck = get_active_deck();
    if (! deck) return;

    context->config_mem_fragmentation += context->tasks.count;
    context->tasks.count = 0;

    tmem_new(tm);
    String file = fs_read_entire_file(context->config_mem, buf_get_str(deck->path, tm), 0);
    add_tasks(file, false);
}

static Void save_config (Bool save_deck) {
    if (save_deck) save_active_deck();

    tmem_new(tm);
    AString astr = astr_new(tm);

    astr_push_fmt(&astr, "version = %lu\n", context->config_version);
    astr_push_fmt(&astr, "editor_width = %f\n", context->editor_width);
    astr_push_fmt(&astr, "editor_height = %f\n", context->editor_height);

    astr_push_cstr(&astr, "sort = [\n");
    array_iter (sort, &context->sorts) {
        astr_push_cstr(&astr, "    {\n");
        astr_push_fmt(&astr, "         ascending = %s\n", sort.ascending ? "true" : "false");
        astr_push_fmt(&astr, "         by        = %u\n", sort.by);
        astr_push_cstr(&astr, "    }\n");
    }
    astr_push_cstr(&astr, "]\n");

    astr_push_cstr(&astr, "decks = [\n");
    array_iter (deck, &context->decks, *) {
        String path = buf_get_str(deck->path, tm);
        String filters = buf_get_str(deck->filters, tm);
        astr_push_cstr(&astr, "    {\n");
        astr_push_fmt(&astr,  "        active  = %s\n", deck->active ? "true" : "false");
        astr_push_fmt(&astr,  "        path    = \"%.*s\"\n", STR(path));
        astr_push_fmt(&astr,  "        filters = \"%.*s\"\n", STR(filters));
        astr_push_cstr(&astr, "    }\n");
    }
    astr_push_cstr(&astr, "]\n");

    fs_ensure_file(context->config_file_path, (String){});
    fs_write_entire_file(context->config_file_path, astr_to_str(&astr));
}

static Void load_config () {
    tmem_new(tm);

    context->config_mem_fragmentation = 0;
    arena_pop_all(cast(Arena*, context->config_mem));
    array_init(&context->sorts, context->config_mem);
    array_init(&context->decks, context->config_mem);
    array_init(&context->tasks, context->config_mem);
    array_init(&context->non_tasks, context->config_mem);

    fs_ensure_file(context->config_file_path, str("data/settings/todo.txt"));

    Config *cfg = config_parse(tm, context->config_file_path);

    U64 disk_version = config_get_u64(cfg, cfg->root, "version");
    if (disk_version != context->config_version) {
        fs_delete_file(context->config_file_path);
        load_config();
        return;
    }

    context->editor_width  = config_get_f64(cfg, cfg->root, "editor_width");
    context->editor_height = config_get_f64(cfg, cfg->root, "editor_height");

    ConfigAst *sort = config_get_array(cfg, cfg->root, "sort");
    array_iter (sort, &sort->children) {
        Sort s = {
            .ascending = config_get_bool(cfg, sort, "ascending"),
            .by = config_get_u64(cfg, sort, "by"),
        };
        array_push(&context->sorts, s);
    }

    ConfigAst *decks = config_get_array(cfg, cfg->root, "decks");
    array_iter (deck_ast, &decks->children) {
        Deck deck = {
            .active  = config_get_bool(cfg, deck_ast, "active"),
            .path    = buf_new(context->config_mem, config_get_string(cfg, deck_ast, "path", tm)),
            .filters = buf_new(context->config_mem, config_get_string(cfg, deck_ast, "filters", tm)),
        };
        array_push(&context->decks, deck);
    }

    load_active_deck();
}

static Void build_task (U64 idx, Bool *out_deleted) {
    tmem_new(tm);

    Task *task = array_ref(&context->tasks, idx);

    UiBox *box = ui_box_fmt(UI_BOX_CAN_FOCUS, "card%lu", idx) {
        ui_tag("card");

        ui_box(0, "header") {
            Bool checked = task->config->flags & MARKUP_AST_META_CONFIG_HAS_DONE;
            UiBox *checkbox = ui_checkbox("checkbox", &checked);
            if (checkbox->signals.clicked) {
                push_command(.tag=CMD_NEW_TASK_FLAGS, .idx=idx, .flags=(task->config->flags ^ MARKUP_AST_META_CONFIG_HAS_DONE));
                push_command(.tag=CMD_VIEW_KANBAN);
            }

            ui_hspacer();

            if (ui_within_box(box->rect, ui->mouse) || (ui->focused && ui_is_descendant(box, ui->focused))) {
                ui_box(0, "autohide_icons") {
                    UiBox *edit_button = ui_box(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "edit") {
                        ui_tag("button");
                        ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_EDIT);
                        if (edit_button->signals.clicked) push_command(.tag=CMD_VIEW_EDITOR, .idx=idx);
                    }

                    UiBox *delete_button = ui_box(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "delete") {
                        ui_tag("button");
                        ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_TRASH);
                        if (delete_button->signals.clicked) push_command(.tag=CMD_DEL_TASK, .idx=idx);
                    }

                    if (! (task->config->flags & MARKUP_AST_META_CONFIG_HAS_PIN)) {
                        UiBox *pin_button = ui_box(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "pin") {
                            ui_tag("button");
                            ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_PIN);
                            if (pin_button->signals.clicked) {
                                push_command(.tag=CMD_NEW_TASK_FLAGS, .idx=idx, .flags=(task->config->flags | MARKUP_AST_META_CONFIG_HAS_PIN));
                                push_command(.tag=CMD_VIEW_KANBAN);
                            }
                        }
                    }
                }
            }

            if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_PIN) {
                UiBox *pin_button = ui_box(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "pin") {
                    ui_style_rule(".button *") { ui_style_vec4(UI_TEXT_COLOR, ui->theme->text_color_green); }
                    ui_style_rule(".button.hover *") { ui_style_vec4(UI_TEXT_COLOR, ui->theme->text_color_blue); }

                    ui_tag("button");
                    ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_PIN);
                    pin_button->next_style.size.width.strictness = 1;
                    if (pin_button->signals.clicked) {
                        push_command(.tag=CMD_NEW_TASK_FLAGS, .idx=idx, .flags=(task->config->flags & ~MARKUP_AST_META_CONFIG_HAS_PIN));
                        push_command(.tag=CMD_VIEW_KANBAN);
                    }
                }
            }

            if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_HIDE) {
                UiBox *hide_button = ui_box(0, "hide") {
                    hide_button->next_style.size.width.strictness = 1;
                    ui_tag("button");
                    ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_HIDDEN);
                }
            }

            if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_PRIORITY) {
                String label = astr_fmt(tm, "#%lu", task->config->priority);
                UiBox *labelb = ui_label(UI_BOX_CLICK_THROUGH, "label", label);
                ui_style_box_vec4(labelb, UI_TEXT_COLOR, ui->theme->text_color_red);
            }
        }

        ui_style_rule(".tag_button") {
            ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z2);
            ui_style_vec2(UI_PADDING, vec2(3, 2));
        }

        if (task->config->flags & (MARKUP_AST_META_CONFIG_HAS_DUE | MARKUP_AST_META_CONFIG_HAS_CREATED | MARKUP_AST_META_CONFIG_HAS_COMPLETED)) {
            ui_box(0, "dates") {
                ui_style_f32(UI_SPACING, ui->theme->spacing);
                ui_style_vec2(UI_PADDING, ui->theme->padding);

                if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_DUE) {
                    ui_button(str("due_date")) {
                        ui_tag("tag_button");
                        String label = astr_fmt(tm, "%s: %.*s", "Due", STR(task->config->due));
                        UiBox *labelb = ui_label(UI_BOX_CLICK_THROUGH, "label", label);
                        ui_style_box_vec4(labelb, UI_TEXT_COLOR, ui->theme->text_color_red);
                    }
                }

                if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_CREATED) {
                    ui_button(str("created_date")) {
                        ui_tag("tag_button");
                        String label = astr_fmt(tm, "%s: %.*s", "Created", STR(task->config->created));
                        UiBox *labelb = ui_label(UI_BOX_CLICK_THROUGH, "label", label);
                        ui_style_box_vec4(labelb, UI_TEXT_COLOR, ui->theme->text_color_green);
                    }
                }

                if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_COMPLETED) {
                    ui_button(str("completed_date")) {
                        ui_tag("tag_button");
                        String label = astr_fmt(tm, "%s: %.*s", "Completed", STR(task->config->completed));
                        UiBox *labelb = ui_label(UI_BOX_CLICK_THROUGH, "label", label);
                        ui_style_box_vec4(labelb, UI_TEXT_COLOR, ui->theme->text_color_green);
                    }
                }
            }
        }

        if (task->config->tags.umap.count) {
            ui_box(0, "tags") {
                ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
                ui_style_f32(UI_SPACING, ui->theme->spacing);
                ui_style_vec2(UI_PADDING, ui->theme->padding);

                U64 col_count = 0;
                U64 row_count = 0;
                UiBox *box = ui_box(0, "row0");
                ui_style_box_f32(box, UI_SPACING, ui->theme->spacing);

                map_iter (slot, &task->config->tags) {
                    if (col_count == 3) {
                        col_count = 0;
                        row_count++;
                        box = ui_box_fmt(0, "row%lu", row_count);
                        ui_style_box_f32(box, UI_SPACING, ui->theme->spacing);
                    }

                    ui_parent(box) {
                        UiBox *tag_button = ui_button(astr_fmt(tm, "tag%lu", col_count)) {
                            ui_tag("tag_button");
                            tag_button->flags |= UI_BOX_CLIPPING;
                            String label = astr_fmt(tm, "%.*s", STR(slot->key));
                            UiBox *labelb = ui_label(UI_BOX_CLICK_THROUGH, "label", label);
                            ui_style_box_vec4(labelb, UI_TEXT_COLOR, ui->theme->text_color_yellow);
                            if (tag_button->signals.clicked) push_command(.tag=CMD_VIEW_SEARCH, .str=str_copy(context->view_mem, label));
                        }
                    }

                    col_count++;
                }
            }
        }

        ui_box(0, "body") {
            String text = markup_ast_get_text(task->ast, task->text);
            String clicked_tag = {};
            ui_markup_view(str("markup"), text, &clicked_tag);
            if (clicked_tag.data) push_command(.tag=CMD_VIEW_SEARCH, .str=clicked_tag);
        }
    }
}

static Void build_view_editor () {
    Auto view = &context->view.editor;

    UiBox *editor = 0;
    UiBox *preview = 0;

    ui_scroll_box(str("left_box"), true) {
        ui_tag("sidebar");
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_CHILDREN_SUM, 0, 1});

        F32 r = ui->theme->radius.x;
        F32 w = context->editor_width;
        F32 h = context->editor_height;

        ui_box(UI_BOX_INVISIBLE_BG, "message") {
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);

            UiBox *header = ui_box(0, "header") {
                ui_style_vec2(UI_PADDING, ui->theme->padding);
                ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);
                ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
                ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);
                ui_style_vec4(UI_RADIUS, vec4(r, r, 0, 0));
                ui_style_box_size(header, UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});

                ui_label(0, "title", str("Tasks"));
            }

            editor = ui_ted_resizable(str("editor"), view->buf, &context->editor_width, &context->editor_height, LINE_WRAP_NONE);
            ui_style_box_vec4(editor, UI_RADIUS, vec4(0, 0, r, r));
            ui_style_box_u32(editor, UI_ANIMATION, UI_MASK_WIDTH);
        }

        if (w != context->editor_width || h != context->editor_height) save_config(false);

        ui_box(0, "buttons") {
            ui_style_f32(UI_SPACING, ui->theme->spacing);

            ui_button_group(str("linked")) {
                UiBox *cancel_button = ui_button(str("cancel")) {
                    ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                    ui_label(UI_BOX_CLICK_THROUGH, "label", str("Cancel"));
                    if (cancel_button->signals.clicked) push_command(.tag=CMD_VIEW_KANBAN);
                }

                UiBox *ok_button = ui_button(str("ok")) {
                    ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                    ui_label(UI_BOX_CLICK_THROUGH, "label", str("Ok"));
                    if (ok_button->signals.clicked) {
                        if (view->task_idx != ARRAY_NIL_IDX) push_command(.tag=CMD_DEL_TASK, .idx=view->task_idx, .skip_config_save=true);
                        String str = buf_get_str(view->buf, context->config_mem);
                        push_command(.tag=CMD_ADD_TASKS, .str=str);
                        push_command(.tag=CMD_VIEW_KANBAN);
                    }
                }
            }

            ui_button_info_popup(str("help_button"), true, str("data/docs/todo.txt"), true);
        }
    }

    UiBox *scrollbox = ui_scroll_box(str("right_box"), true) {
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_u32(UI_ALIGN_X, UI_ALIGN_MIDDLE);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_f32(UI_SPACING, ui->theme->spacing);
        ui_style_vec2(UI_PADDING, ui->theme->padding);

        F32 r = ui->theme->radius.x;
        F32 b = ui->theme->border_width.x;

        ui_box(UI_BOX_INVISIBLE_BG, "preview") {
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
            ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, 30*ui->config->font_size, 1});

            ui_box(0, "header") {
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 1});
                ui_style_vec2(UI_PADDING, ui->theme->padding);
                ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);
                ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
                ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);
                ui_style_vec4(UI_RADIUS, vec4(r, r, 0, 0));
                ui_label(0, "title", str("Task previews"));
            }

            preview = ui_markup_view_buf(str("markup"), view->buf, true, 0);
            ui_style_box_vec2(preview, UI_PADDING, ui->theme->padding);
            ui_style_box_vec4(preview, UI_BG_COLOR, ui->theme->bg_color_z3);
            ui_style_box_vec4(preview, UI_BORDER_COLOR, ui->theme->border_color);
            ui_style_box_vec4(preview, UI_BORDER_WIDTHS, ui->theme->border_width);
            ui_style_box_vec4(preview, UI_RADIUS, vec4(0, 0, r, r));
            ui_style_box_vec4(preview, UI_BORDER_WIDTHS, vec4(b, 0, b, b));
        }
    }

    if (ui->focused && ui_is_descendant(editor, ui->focused)) {
        app_sync_scroll(scrollbox, editor, preview, &view->cursor);
    }
}

static Int cmp_search_results (Void *a_, Void *b_) {
    SearchResult *a = a_;
    SearchResult *b = b_;
    return (a->score < b->score) ? -1 : (a->score > b->score) ? 1 : 0;
}

static Void build_view_search () {
    tmem_new(tm);
    Auto view = &context->view.search;

    ui_scroll_box(str("left_box"), true) {
        ui_tag("sidebar");
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, .35, 1});

        ui_box(0, "search_box") {
            ui_style_f32(UI_SPACING, ui->theme->spacing);
            ui_entry(str("entry"), view->buf, -1, str("Filter expression..."));
            ui_button_info_popup(str("help_button"), true, str("data/docs/filters.txt"), true);
        }

        ui_box(UI_BOX_INVISIBLE_BG, "row_group") {
            ui_box(0, "row") {
                ui_tag("row");
                ui_label(0, "title", str("Delete searched tasks"));
                ui_hspacer();
                ui_checkbox("checkbox", &view->delete_searched);
            }
        }

        ui_button_group(str("buttons")) {
            UiBox *close_button = ui_button(str("close")) {
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                ui_label(UI_BOX_CLICK_THROUGH, "label", str("Close"));
                if (close_button->signals.clicked) push_command(.tag=CMD_VIEW_KANBAN);
            }

            UiBox *apply_button = ui_button(str("apply")) {
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                ui_label(UI_BOX_CLICK_THROUGH, "label", str("Apply"));
                if (apply_button->signals.clicked) array_iter (idx, &view->searched) push_command(.tag=CMD_DEL_TASK, .idx=idx, .skip_config_save=!ARRAY_ITER_DONE);
            }
        }
    }

    ui_scroll_box(str("right_box"), true) {
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_u32(UI_ALIGN_X, UI_ALIGN_MIDDLE);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_f32(UI_SPACING, ui->theme->spacing);
        ui_style_vec2(UI_PADDING, ui->theme->padding);

        if (view->buf_version != buf_get_version(view->buf)) {
            view->searched.count = 0;
            view->buf_version = buf_get_version(view->buf);

            String filter_text = buf_get_str(view->buf, tm);
            MarkupAst *filter_node = markup_filter_parse(tm, filter_text);

            array_iter (task, &context->tasks, *) {
                if (task_passes_filter(task, filter_node)) {
                    array_push(&view->searched, ARRAY_IDX);
                }
            }
        }

        Bool deleted = false;

        array_iter (task_idx, &view->searched) {
            if (ARRAY_IDX == view->show_more_idx) break;
            build_task(task_idx, &deleted);
        }

        app_show_more_button(str("show_more"), &view->show_more_idx, view->searched.count);

        if (deleted) view->buf_version--; // To refresh the searched array.
    }
}

static Void build_view_sort () {
    Auto view = &context->view.sort;

    ui_box(0, "left_box") {
        ui_tag("sidebar");
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, .35, 1});

        ui_box(0, "items") {
            ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);

            ui_style_rule(".item") {
                F32 b = ui->theme->border_width.x;
                ui_style_vec4(UI_RADIUS, vec4(0, 0, 0, 0));
                ui_style_vec2(UI_PADDING, ui->theme->padding);
                ui_style_f32(UI_SPACING, ui->theme->spacing);
                ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);
                ui_style_vec4(UI_BORDER_WIDTHS, vec4(b, b, b, 0));
                ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
            }

            ui_style_rule(".item:first") {
                F32 r = ui->theme->radius.x;
                ui_style_vec4(UI_RADIUS, vec4(r, r, 0, 0));
            }

            ui_style_rule(".item:last") {
                F32 r = ui->theme->radius.x;
                ui_style_vec4(UI_RADIUS, vec4(0, 0, r, r));
                ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);
            }

            ui_style_rule(".button.hover *") {
                ui_style_vec4(UI_TEXT_COLOR, ui->theme->text_color_blue);
            }

            ui_style_rule(".button.focus") {
                ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color_focus);
                ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width_focus);
            }

            array_iter (sort, &context->sorts) {
                UiBox *box = ui_box_fmt(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "sort%lu", ARRAY_IDX) {
                    ui_tag("item");

                    if (view->dragging && ui_within_box(box->rect, ui->mouse)) {
                        ui_style_rule(".item") ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z1);
                        ui_label(0, "label", str(""));
                        ui_hspacer();
                        if (ui->event->tag == EVENT_KEY_RELEASE && ui->event->key == KEY_MOUSE_LEFT) {
                            view->dragging = false;
                            push_command(.tag=CMD_CHANGE_SORT, .idx=view->draggee, .idx2=ARRAY_IDX);
                        }
                    } else {
                        ui_label(0, "label", sort_to_string(sort.by));
                        ui_hspacer();

                        if (ui_within_box(box->rect, ui->mouse) || (ui->focused && ui_is_descendant(box, ui->focused))) {
                            UiBox *up_button = ui_box(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "up") {
                                ui_tag("button");
                                ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_PAN_UP);
                                up_button->next_style.size.width.strictness = 1;
                                if (up_button->signals.clicked && ARRAY_IDX > 0) push_command(.tag=CMD_CHANGE_SORT, .idx=ARRAY_IDX, .idx2=ARRAY_IDX-1);
                            }

                            UiBox *down_button = ui_box(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "down") {
                                ui_tag("button");
                                ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_PAN_DOWN);
                                down_button->next_style.size.width.strictness = 1;
                                if (down_button->signals.clicked && !ARRAY_ITER_DONE) push_command(.tag=CMD_CHANGE_SORT, .idx=ARRAY_IDX, .idx2=ARRAY_IDX+1);
                            }
                        }

                        UiBox *sort_button = ui_box(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "sort") {
                            ui_tag("button");
                            ui_icon(UI_BOX_CLICK_THROUGH, "icon", sort.ascending ? UI_ICON_SORT_ASC : UI_ICON_SORT_DESC);
                            sort_button->next_style.size.width.strictness = 1;
                            if (sort_button->signals.clicked) push_command(.tag=CMD_CHANGE_SORT, .idx=ARRAY_IDX, .idx2=ARRAY_NIL_IDX);
                        }
                    }

                    if (box->signals.pressed && (ui->event->tag == EVENT_MOUSE_MOVE)) {
                        view->dragging = true;
                        view->draggee = ARRAY_IDX;
                    }
                }
            }

            if (view->dragging) {
                ui_parent(ui->root) {
                    ui_box(0, "preview") {
                        ui_style_f32(UI_FLOAT_X, ui->mouse.x + 10);
                        ui_style_f32(UI_FLOAT_Y, ui->mouse.y + 10);
                        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PIXELS, 10*ui->config->font_size, 1});
                        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PIXELS, 2*ui->config->font_size, 1});
                        ui_style_vec4(UI_BG_COLOR, ui->theme->bg_color_z3);
                        ui_style_vec4(UI_BORDER_WIDTHS, ui->theme->border_width);
                        ui_style_vec4(UI_BORDER_COLOR, ui->theme->border_color);
                    }
                }
            }
        }

        ui_box(0, "buttons") {
            ui_style_f32(UI_SPACING, ui->theme->spacing);

            UiBox *close_button = ui_button(str("close")) {
                ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                ui_label(UI_BOX_CLICK_THROUGH, "label", str("Close"));
                if (close_button->signals.clicked) push_command(.tag=CMD_VIEW_KANBAN);
            }

            ui_button_info_popup(str("help_button"), true, str("data/docs/sort.txt"), true);
        }
    }

    ui_scroll_box(str("right_box"), true) {
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_u32(UI_ALIGN_X, UI_ALIGN_MIDDLE);
        ui_style_f32(UI_SPACING, ui->theme->spacing);
        ui_style_vec2(UI_PADDING, ui->theme->padding);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});

        array_iter (_, &context->tasks, *) {
            _;
            if (ARRAY_IDX == view->show_more) break;
            build_task(ARRAY_IDX, 0);
        }

        app_show_more_button(str("show_more"), &view->show_more, context->tasks.count);
    }

    if (view->dragging && ui->event->tag == EVENT_KEY_RELEASE && ui->event->key == KEY_MOUSE_LEFT) {
        view->dragging = false;
    }
}

static Void build_view_deck_browser () {
    tmem_new(tm);
    Auto view = &context->view.deck_browser;

    ui_box(0, "left_box") {
        ui_tag("sidebar");
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, .35, 1});

        ui_entry(str("search_entry"), view->buf, -1, str("Search decks..."));

        ui_box(0, "buttons") {
            ui_style_f32(UI_SPACING, ui->theme->spacing);

            ui_button_group(str("linked")) {
                UiBox *close_button = ui_button(str("close")) {
                    ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                    ui_label(UI_BOX_CLICK_THROUGH, "label", str("Close"));
                    if (close_button->signals.clicked) {
                        push_command(.tag=CMD_SAVE_CONFIG);
                        push_command(.tag=CMD_VIEW_KANBAN);
                    }
                }

                UiBox *add_button = ui_button(str("add")) {
                    ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
                    ui_label(UI_BOX_CLICK_THROUGH, "label", str("Add"));
                    if (add_button->signals.clicked) push_command(.tag=CMD_NEW_DECK);
                }
            }

            ui_button_info_popup(str("help_button"), true, str("data/docs/todo.txt"), true);
        }
    }

    ui_scroll_box(str("right_box"), true) {
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_u32(UI_ALIGN_X, UI_ALIGN_MIDDLE);
        ui_style_f32(UI_SPACING, ui->theme->spacing);
        ui_style_vec2(UI_PADDING, ui->theme->padding);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});

        { // Search:
            view->searched.count = 0;

            String needle = buf_get_str(view->buf, tm);

            array_iter (deck, &context->decks, *) {
                I64 score = str_fuzzy_search(needle, buf_get_str(deck->path, tm), 0);
                if (score != INT64_MIN) array_push_lit(&view->searched, .score=score, .idx=ARRAY_IDX);
            }

            array_sort_cmp(&view->searched, cmp_search_results);
        }

        array_iter (d, &view->searched) {
            Deck *deck = array_ref(&context->decks, d.idx);

            UiBox *box = ui_box_fmt(UI_BOX_CAN_FOCUS, "deck%lu", d.idx) {
                ui_tag("card");

                ui_box(0, "header") {
                    UiBox *checkbox = ui_checkbox("checkbox", &deck->active);
                    if (checkbox->signals.clicked && deck->active) push_command(.tag=CMD_ACTIVATE_DECK, .idx=d.idx);

                    ui_hspacer();

                    if (ui_within_box(box->rect, ui->mouse) || (ui->focused && ui_is_descendant(box, ui->focused))) {
                        ui_box(0, "autohide_icons") {
                            UiBox *delete_button = ui_box(UI_BOX_CAN_FOCUS|UI_BOX_REACTIVE, "delete") {
                                ui_tag("button");
                                ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_TRASH);
                                if (delete_button->signals.clicked) push_command(.tag=CMD_DEL_DECK, .idx=d.idx);
                            }
                        }
                    }
                }

                ui_box(0, "body") {
                    ui_file_picker_entry(str("file"), deck->path, str("Select file..."), -1, false, false, (String){});
                    ui_entry(str("filters"), deck->filters, -1, str("Comma separated list of filter expressions..."));
                }
            }
        }
    }
}

static Void build_view_kanban () {
    Auto view = &context->view.kanban;

    ui_box(0, "navbox") {
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_vec2(UI_PADDING, ui->theme->padding);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_CHILDREN_SUM, 0, 1});

        ui_style_rule(".button")       { ui_style_vec4(UI_RADIUS, vec4(0, 0, 0, 0)); }
        ui_style_rule(".button:first") { ui_style_vec4(UI_RADIUS, vec4(ui->theme->radius.x, ui->theme->radius.x, 0, 0)); }
        ui_style_rule(".button:last")  { ui_style_vec4(UI_RADIUS, vec4(0, 0, ui->theme->radius.x, ui->theme->radius.x)); }

        UiBox *add_button = ui_button(str("add")) {
            ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_PLUS);
            if (add_button->signals.clicked) push_command(.tag=CMD_VIEW_EDITOR, .idx=ARRAY_NIL_IDX);
            if (add_button->signals.hovered) { ui_tooltip(str("tooltip")) ui_label(0, "tooltip", str("Add task")); }
        }

        UiBox *search_button = ui_button(str("search")) {
            ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_SEARCH);
            if (search_button->signals.clicked) push_command(.tag=CMD_VIEW_SEARCH);
            if (search_button->signals.hovered) { ui_tooltip(str("tooltip")) ui_label(0, "tooltip", str("Search tasks")); }
        }

        UiBox *deck_button = ui_button(str("deck")) {
            ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_FOLDER);
            if (deck_button->signals.clicked) push_command(.tag=CMD_VIEW_DECK_BROWSER);
            if (deck_button->signals.hovered) { ui_tooltip(str("tooltip")) ui_label(0, "tooltip", str("Browse decks")); }
        }

        UiBox *sort_button = ui_button(str("sort")) {
            ui_icon(UI_BOX_CLICK_THROUGH, "icon", UI_ICON_SORT_ASC);
            if (sort_button->signals.clicked) push_command(.tag=CMD_VIEW_SORT);
            if (sort_button->signals.hovered) { ui_tooltip(str("tooltip")) ui_label(0, "tooltip", str("Sort tasks")); }
        }
    }

    ui_scroll_box(str("cards"), true) {
        ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
        ui_style_f32(UI_SPACING, ui->theme->spacing);
        ui_style_size(UI_WIDTH, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        ui_style_size(UI_HEIGHT, (UiSize){UI_SIZE_PCT_PARENT, 1, 0});
        if (! view->has_filters) ui_style_u32(UI_ALIGN_X, UI_ALIGN_MIDDLE);

        ui_box(0, "cards") {
            ui_style_f32(UI_SPACING, ui->theme->spacing);
            ui_style_vec2(UI_PADDING, ui->theme->padding);

            array_iter (column, &view->columns) {
                ui_box_fmt(0, "column%lu", ARRAY_IDX) {
                    ui_style_u32(UI_AXIS, UI_AXIS_VERTICAL);
                    ui_style_f32(UI_SPACING, ui->theme->spacing);

                    if (column->with_header) {
                        ui_box(0, "header") {
                            ui_tag("card");
                            ui_style_vec2(UI_PADDING, ui->theme->padding);
                            ui_label_extra(0, "filter", column->filter_text, ui->config->font_path_bold, ui->config->font_size, false);
                        }
                    }

                    array_iter (task, &column->tasks) {
                        if (ARRAY_IDX == column->show_more_idx) break;
                        build_task(task, 0);
                    }

                    app_show_more_button(str("show_more"), &column->show_more_idx, column->tasks.count);
                }
            }
        }
    }
}

Void todo_view_init (UiViewInstance *) {
}

Void todo_view_free (UiViewInstance *) {
}

UiIcon todo_view_get_icon (UiViewInstance *, Bool visible) {
    return UI_ICON_TODO;
}

String todo_view_get_title (UiViewInstance *, Bool visible) {
    return str("Todo");
}

static Void destroy_current_view () {
    switch (context->view.tag) {
    case VIEW_KANBAN: break;
    case VIEW_EDITOR: break;
    case VIEW_SORT: break;
    case VIEW_SEARCH: break;
    case VIEW_DECK_BROWSER: break;
    }

    arena_pop_all(cast(Arena*, context->view_mem));
    context->view = (View){};
}

static Void execute_commands () {
    tmem_new(tm);

    array_iter (cmd, &context->commands, *) {
        switch (cmd->tag) {
        case CMD_SAVE_CONFIG: {
            save_config(cmd->save_deck);
        } break;

        case CMD_NEW_TASK_FLAGS: {
            Task *task = array_ref(&context->tasks, cmd->idx);
            task->config->flags = cmd->flags;
            task_serialize(cmd->idx);
            sort_tasks();
            if (! cmd->skip_config_save) save_config(true);
        } break;

        case CMD_ADD_TASKS: {
            add_tasks(cmd->str, false);
            sort_tasks();
            if (! cmd->skip_config_save) save_config(true);
        } break;

        case CMD_DEL_TASK: {
            array_remove(&context->tasks, cmd->idx);
            context->config_mem_fragmentation++;
            if (! cmd->skip_config_save) save_config(true);
        } break;

        case CMD_NEW_DECK: {
            Deck *deck = array_push_slot(&context->decks);
            deck->active = false;
            deck->path = buf_new(context->config_mem, str(""));
            deck->filters = buf_new(context->config_mem, str(""));
            if (! cmd->skip_config_save) save_config(false);
        } break;

        case CMD_DEL_DECK: {
            array_remove(&context->decks, cmd->idx);
            context->config_mem_fragmentation++;
            load_active_deck();
            if (! cmd->skip_config_save) save_config(false);
        } break;

        case CMD_ACTIVATE_DECK: {
            array_iter (deck, &context->decks, *) deck->active == (ARRAY_IDX == cmd->idx);
            load_active_deck();
            if (! cmd->skip_config_save) save_config(false);
        } break;

        case CMD_CHANGE_SORT: {
            if (cmd->idx2 == ARRAY_NIL_IDX) {
                Sort *s = array_ref(&context->sorts, cmd->idx);
                s->ascending = !s->ascending;
            } else {
                array_swap(&context->sorts, cmd->idx, cmd->idx2);
            }
            sort_tasks();
            if (! cmd->skip_config_save) save_config(true);
        } break;

        case CMD_VIEW_KANBAN: {
            destroy_current_view();
            context->view.tag = VIEW_KANBAN;

            Auto view = &context->view.kanban;

            array_init(&view->columns, context->view_mem);
            array_init(&view->filters, context->view_mem);

            view->has_filters = false;
            Deck *deck = get_active_deck();

            if (deck) {
                String s = buf_get_str(deck->filters, context->view_mem);
                str_split(s, str(","), false, false, &view->filters);
                if (s.count) {
                    view->has_filters = true;
                } else {
                    array_push(&view->filters, str("* & !hide"));
                }
            }

            array_iter (filter, &view->filters) {
                MarkupAst *filter_node = markup_filter_parse(context->view_mem, filter);
                KanbanColumn *col = mem_new(context->view_mem, KanbanColumn);
                col->filter_text = filter;
                col->filter_node = filter_node;
                col->with_header = view->has_filters;
                array_init(&col->tasks, context->view_mem);
                array_push(&view->columns, col);
            }

            array_iter (task, &context->tasks, *) {
                U64 idx = ARRAY_IDX;
                array_iter (column, &view->columns) {
                    if (task_passes_filter(task, column->filter_node)) {
                        array_push(&column->tasks, idx);
                        break;
                    }
                }
            }
        } break;

        case CMD_VIEW_SORT: {
            destroy_current_view();
            context->view.tag = VIEW_SORT;
        } break;

        case CMD_VIEW_DECK_BROWSER: {
            destroy_current_view();
            context->view.tag = VIEW_DECK_BROWSER;
            context->view.deck_browser.buf = buf_new(context->view_mem, str(""));
            array_init(&context->view.deck_browser.searched, context->view_mem);
        } break;

        case CMD_VIEW_SEARCH: {
            String needle = cmd->str.data ? str_copy(tm, cmd->str) : str("");
            destroy_current_view();
            context->view.tag = VIEW_SEARCH;
            context->view.search.buf = buf_new(context->view_mem, needle);
            array_init(&context->view.search.searched, context->view_mem);
        } break;

        case CMD_VIEW_EDITOR: {
            destroy_current_view();
            context->view.tag = VIEW_EDITOR;
            context->view.editor.task_idx = cmd->idx;
            Task *task = (cmd->idx == ARRAY_NIL_IDX) ? 0 : array_ref(&context->tasks, cmd->idx);
            String init_text;
            if (task) {
                init_text = markup_ast_get_text(task->ast, task->text);
            } else {
                Date d = os_get_date();
                init_text = astr_fmt(tm, "[created: %u-%02u-%02u] ", d.year, d.month, d.day);
            }
            context->view.editor.buf = buf_new(context->view_mem, init_text);
            if (task) context->view.editor.task = *task;
        } break;
        }
    }

    context->commands.count = 0;
}

Void todo_view_build (UiViewInstance *, Bool visible) {
    if (context->decks.count == 0 && context->view.tag != VIEW_DECK_BROWSER) {
        push_command(.tag=CMD_VIEW_DECK_BROWSER);
    }

    execute_commands();
    if (context->config_mem_fragmentation > 100) load_config();

    switch (context->view.tag) {
    case VIEW_KANBAN: build_view_kanban(); break;
    case VIEW_EDITOR: build_view_editor(); break;
    case VIEW_SEARCH: build_view_search(); break;
    case VIEW_SORT: build_view_sort(); break;
    case VIEW_DECK_BROWSER: build_view_deck_browser(); break;
    }
}

Void todo_init () {
    if (context) return;

    context = mem_new(mem_root, Context);
    context->config_version = 0;
    context->view_mem = cast(Mem*, arena_new(mem_root, 1*KB));
    context->config_mem = cast(Mem*, arena_new(mem_root, 1*KB));
    array_init(&context->commands, mem_root);

    { // Build config file path:
        tmem_new(tm);
        AString a = astr_new(mem_root);
        astr_push_str(&a, fs_get_home_dir(tm));
        astr_push_cstr(&a, "/.config/chronomi/todo.txt");
        context->config_file_path = astr_to_str(&a);
    }

    load_config();
    push_command(.tag=CMD_VIEW_KANBAN);
}
