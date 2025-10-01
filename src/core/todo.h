#pragma once

#include "core/markup.h"
#include "util/mem.h"
#include "util/core.h"
#include "util/string.h"
#include "os/time.h"

enum SortBy {
    SORT_BY_PIN,
    SORT_BY_DUE,
    SORT_BY_PRIORITY,
    SORT_BY_DONE,
    SORT_BY_HIDE,
    SORT_COUNT
};

struct Sort {
    Bool ascending;
    SortBy by;
};

struct Deck {
    Bool active;
    String path;
    String filters;
};

struct Task {
    String text; // Potentially bigger text that contains the task.
    MarkupAst *ast;
    MarkupAstMetaConfig *config;
};

// For now all memory for the loaded file sits in the deck_arena.
// Deleting tasks is a nop which leads to fragmentation. In order
// to defragment the arena, call todo todo_load_file(). We track
// the number of deleted/updated tasks with the fragmentation var.
//
// @todo Maybe we should put all memory into an arena instead of
// just the memory related to the loaded deck
struct TodoApplet {
    Mem *mem;
    Arena *deck_arena;
    U64 fragmentation;
    Array<Sort> sorts;
    Array<Deck*> decks;
    Array<Task*> tasks;
    Array<String> non_tasks;
};

TodoApplet *todo_new             (Mem *);
Deck       *todo_deck_add        (TodoApplet *, Bool, String, String);
Void        todo_deck_del        (TodoApplet *, Deck *);
Deck       *todo_get_active_deck (TodoApplet *);
Bool        todo_get_filters     (TodoApplet *, Array<String> *out, String alt);
Void        todo_load_file       (TodoApplet *);
Void        todo_flush_file      (TodoApplet *);
Bool        todo_passes_filter   (Task *, MarkupAst *);
I64         todo_compare_tasks   (Task *, Task *, Slice<Sort>);
Void        todo_task_update     (TodoApplet *, Task *);
Void        todo_tasks_add       (TodoApplet *, String, Bool);
Task       *todo_task_add        (TodoApplet *, String, MarkupAst *);
Void        todo_task_del        (TodoApplet *, Task *);
Bool        todo_is_valid_task   (TodoApplet *, MarkupAst *);
