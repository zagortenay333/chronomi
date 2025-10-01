#pragma once

#include "ui/widgets.h"
#include "util/core.h"
#include "util/string.h"
#include "util/mem.h"

#define tr(...) __VA_ARGS__

const U64 SPACING = 8;

struct View {
    Mem *arena;
    UiScrollbox *box_left_scroll;
    UiScrollbox *box_right_scroll;
    GtkWidget   *box_left;
    GtkWidget   *box_right;
};

template <typename T>
static T *ui_view_new (U64 block_size, GtkWidget *parent, Bool left_box_is_sidebar, Bool right_box_scrolls) {
    Auto arena = arena_new(&mem_root, block_size);
    Auto view = mem_new(arena, T);
    view->base.arena = &arena->base;
    ui_view_init(&view->base, parent, left_box_is_sidebar, right_box_scrolls);
    return view;
}

Void   ui_view_init           (View *, GtkWidget *, Bool, Bool);
Void   ui_view_destroy        (View *);
Void   ui_idle_clear_arena    (Mem *, U64, Arena **);
String ui_rgba_to_string      (Mem *, GdkRGBA *);
Void   ui_open_web_uri        (String uri);
Void   ui_open_file_uri       (String path);
Void   ui_set_cursor          (CString name);
Void   ui_set_label_font_size (GtkLabel *, Int);
Void   ui_scroll_to_widget    (GtkWidget *);
