#include "core/todo.h"
#include "os/fs.h"

TodoApplet *todo_new (Mem *mem) {
    Auto applet = mem_new(mem, TodoApplet);
    applet->mem = mem;
    applet->deck_arena = arena_new(mem, 4*KB);
    array_init(&applet->sorts, applet->mem);
    array_init(&applet->decks, applet->mem);
    array_init(&applet->tasks, &applet->deck_arena->base);
    array_init(&applet->non_tasks, &applet->deck_arena->base);
    return applet;
}

Void todo_load_file (TodoApplet *applet) {
    applet->fragmentation = 0;
    arena_pop_all(applet->deck_arena);
    array_init(&applet->tasks, &applet->deck_arena->base);
    array_init(&applet->non_tasks, &applet->deck_arena->base);

    Deck *deck = todo_get_active_deck(applet);

    if (deck) {
        String file = fs_read_entire_file(&applet->deck_arena->base, deck->path, 0);
        todo_tasks_add(applet, file, false);
    }
}

Void todo_flush_file (TodoApplet *applet) {
    tmem_new(tm);

    AString content = astr_new(tm);

    array_iter (task, &applet->tasks) {
        String text = markup_ast_get_text(task->ast, task->text);
        astr_push_str(&content, text);
    }

    array_iter (non_task, &applet->non_tasks) {
        astr_push_str(&content, non_task);
    }

    Deck *deck = todo_get_active_deck(applet);
    if (deck) fs_write_entire_file(deck->path, astr_to_str(&content));
}

static Bool todo_passes_filter_ (Task *task, MarkupAst *filter) {
    MarkupAstMetaConfig *c = task->config;
    String text = markup_ast_get_text(task->ast, task->text);
    MarkupAst *op1 = array_try_get(&filter->children, 0);
    MarkupAst *op2 = array_try_get(&filter->children, 1);

    switch (filter->tag) {
    case MARKUP_AST_FILTER_NOT:      return !todo_passes_filter_(task, op1);
    case MARKUP_AST_FILTER_OR:       return todo_passes_filter_(task, op1) || todo_passes_filter_(task, op2);
    case MARKUP_AST_FILTER_AND:      return todo_passes_filter_(task, op1) && todo_passes_filter_(task, op2);
    case MARKUP_AST_FILTER_ANY:      return true;
    case MARKUP_AST_FILTER_DUE:      return c->flags & MARKUP_AST_META_CONFIG_HAS_DUE;
    case MARKUP_AST_FILTER_DONE:     return c->flags & MARKUP_AST_META_CONFIG_HAS_DONE;
    case MARKUP_AST_FILTER_PIN:      return c->flags & MARKUP_AST_META_CONFIG_HAS_PIN;
    case MARKUP_AST_FILTER_HIDE:     return c->flags & MARKUP_AST_META_CONFIG_HAS_HIDE;
    case MARKUP_AST_FILTER_TAG:      return (c->flags & MARKUP_AST_META_CONFIG_HAS_TAGS) && map_has(&c->tags, reinterpret_cast<MarkupAstFilterTag*>(filter)->text);
    case MARKUP_AST_FILTER_TRACK:    return (c->flags & MARKUP_AST_META_CONFIG_HAS_TRACK) && (reinterpret_cast<MarkupAstFilterTrack*>(filter)->id == c->track);
    case MARKUP_AST_FILTER_FUZZY:    return str_fuzzy_search(reinterpret_cast<MarkupAstFilterFuzzy*>(filter)->needle, text, 0) != INT64_MIN;
    case MARKUP_AST_FILTER_STRING:   return str_search(reinterpret_cast<MarkupAstFilterString*>(filter)->needle, text) != ARRAY_NIL_IDX;
    case MARKUP_AST_FILTER_PRIORITY: return (c->flags & MARKUP_AST_META_CONFIG_HAS_PRIORITY) && (reinterpret_cast<MarkupAstFilterPriority*>(filter)->priority == c->priority);
    case MARKUP_AST_FILTER_ERROR:    return false;
    default: badpath;
    }
}

Bool todo_passes_filter (Task *task, MarkupAst *filter) {
    MarkupAst *op1 = array_try_get(&filter->children, 0);

    if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_HIDE) {
        if (filter->tag == MARKUP_AST_FILTER_ANY || filter->tag == MARKUP_AST_FILTER_HIDE) return true;
        if (! (filter->tag == MARKUP_AST_FILTER_AND && op1->tag == MARKUP_AST_FILTER_HIDE)) return false;
    }

    return todo_passes_filter_(task, filter);
}

I64 todo_compare_tasks (Task *a, Task *b, Slice<Sort> sorts) {
    array_iter (sort, &sorts) {
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

Deck *todo_deck_add (TodoApplet *applet, Bool active, String path, String filters) {
    Auto deck = mem_new(applet->mem, Deck);
    deck->active = active;
    deck->path = str_copy(applet->mem, path);
    deck->filters = str_copy(applet->mem, filters);
    array_push(&applet->decks, deck);
    return deck;
}

Void todo_deck_del (TodoApplet *applet, Deck *deck) {
    array_find_remove(&applet->decks, [&](Deck *it){ return it == deck; });
    array_maybe_decrease_capacity(&applet->decks);
    mem_free(applet->mem, .old_ptr=deck->path.data, .old_size=deck->path.count);
    mem_free(applet->mem, .old_ptr=deck->filters.data, .old_size=deck->filters.count);
    mem_free(applet->mem, .old_ptr=deck, .old_size=sizeof(deck));
}

Deck *todo_get_active_deck (TodoApplet *applet) {
    array_iter (deck, &applet->decks) if (deck->active) return deck;
    return array_try_get(&applet->decks, 0);
}

Bool todo_get_filters (TodoApplet *applet, Array<String> *out, String alt) {
    Deck *deck = todo_get_active_deck(applet);
    if (! deck) return false;

    str_split(deck->filters, str(","), false, false, out);

    if (deck->filters.count == 0) {
        array_push(out, alt);
        return false;
    } else {
        return true;
    }
}

Void todo_task_update (TodoApplet *applet, Task *task) {
    AString a = astr_new(&applet->deck_arena->base);

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
    MarkupAst *root = markup_parse(&applet->deck_arena->base, task->text);
    task->ast = array_get(&root->children, 0);
    assert_always(task->ast->tag == MARKUP_AST_META);
    task->config = reinterpret_cast<MarkupAstMeta*>(task->ast)->config;

    applet->fragmentation++;
}

Void todo_tasks_add (TodoApplet *applet, String text, Bool copy_text) {
    if (copy_text) text = str_copy(&applet->deck_arena->base, text);

    MarkupAst *root = markup_parse(&applet->deck_arena->base, text);

    array_iter (node, &root->children) {
        if (todo_is_valid_task(applet, node)) {
            todo_task_add(applet, text, node);
        } else {
            String text = markup_ast_get_text(node, text);
            array_push(&applet->non_tasks, text);
        }
    }
}

Bool todo_is_valid_task (TodoApplet *, MarkupAst *node) {
    if (node->tag != MARKUP_AST_META) return false;
    Auto n = reinterpret_cast<MarkupAstMeta*>(node);
    return !(n->config->flags & ~MARKUP_AST_META_TODO_FLAGS);
}

Task *todo_task_add (TodoApplet *applet, String text, MarkupAst *ast) {
    Auto task = mem_new(&applet->deck_arena->base, Task);
    task->text = text;
    task->ast = ast;
    assert_always(ast->tag == MARKUP_AST_META);
    task->config = reinterpret_cast<MarkupAstMeta*>(ast)->config;
    array_push(&applet->tasks, task);
    return task;
}

Void todo_task_del (TodoApplet *applet, Task *task) {
    array_find_remove(&applet->tasks, [&](Task *it){ return it == task; });
    applet->fragmentation++;
}
