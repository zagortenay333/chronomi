#pragma once

#include "base/core.h"
#include "ui/ui.h"
#include "ui/ui_view.h"
#include "ui/ui_tile.h"

ienum (ViewId, U32) {
    VIEW_SETTINGS = 0,
    VIEW_TODO = 1,
    VIEW_ALARM = 2,
    VIEW_TIMER = 3,
    VIEW_POMODORO = 4,
    VIEW_STOPWATCH = 5,
    VIEW_FLASHCARDS = 6,
    VIEW_HELP = 7,
    VIEW_COUNT,
};

istruct (SearchResult) {
    I64 score;
    U64 idx;
};

istruct (App) {
    U64 config_version;
    String config_dir_path;
    String config_file_path;
    String theme_file_path;

    UiViewStore *views;

    Mem *config_mem;
    UiTileNode *tile_tree;
    UiConfig ui_config;
    UiTheme ui_theme;
};

extern App *app;

Void   app_init                      ();
Void   app_build                     ();
Void   app_config_save               ();
Void   app_load_ui_theme             (String filepath);
Void   app_sync_scroll               (UiBox *scrollbox, UiBox *editor, UiBox *markup, U64 *prev_cursor);
UiBox *app_show_more_button          (String id, U64 *idx, U64 count);
Int    app_cmp_search_results        (Void *, Void *);
Int    app_cmp_search_results_on_idx (Void *, Void *);
