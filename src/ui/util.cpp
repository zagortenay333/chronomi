#include "ui/main.h"
#include "ui/util.h"
#include "ui/widgets.h"

Void ui_view_destroy (View *view) {
    gtk_widget_unparent(view->box_left_scroll->widget);

    if (view->box_right_scroll) {
        gtk_widget_unparent(view->box_right_scroll->widget);
    } else {
        gtk_widget_unparent(view->box_right);
    }

    ui_idle_clear_arena(&mem_root, 4*KB, reinterpret_cast<Arena**>(&view->arena));
}

Void ui_view_init (View *view, GtkWidget *parent, Bool left_box_is_sidebar, Bool right_box_scrolls) {
    UiScrollbox *box_left = ui_scrollbox_new(view->arena, true);
    gtk_box_append(GTK_BOX(parent), box_left->widget);
    view->box_left_scroll = box_left;
    view->box_left = box_left->box;

    if (left_box_is_sidebar) {
        gtk_widget_add_css_class(box_left->widget, "kronomi-sidebar");
        gtk_widget_add_css_class(box_left->widget, "frame");
        gtk_widget_set_hexpand(box_left->widget, false);
        gtk_widget_set_vexpand(box_left->widget, true);
    } else {
        gtk_widget_add_css_class(box_left->widget, "kronomi-floating-button-box");
    }

    if (right_box_scrolls) {
        UiScrollbox *box_right = ui_scrollbox_new(view->arena, true);
        gtk_box_append(GTK_BOX(parent), box_right->widget);
        gtk_widget_add_css_class(box_right->widget, "kronomi-main-box");
        view->box_right_scroll = box_right;
        view->box_right = box_right->box;
        gtk_widget_set_hexpand(box_right->widget, true);
        gtk_widget_set_halign(box_right->widget, GTK_ALIGN_FILL);
        gtk_widget_set_hexpand(box_right->box, false);
        gtk_widget_set_halign(box_right->box, GTK_ALIGN_CENTER);
    } else {
        GtkWidget *box_right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_box_append(GTK_BOX(parent), box_right);
        gtk_widget_add_css_class(box_right, "kronomi-main-box");
        gtk_widget_set_hexpand(box_right, true);
        gtk_widget_set_vexpand(box_right, true);
        gtk_widget_set_halign(box_right, GTK_ALIGN_FILL);
        gtk_widget_set_valign(box_right, GTK_ALIGN_FILL);
        view->box_right = box_right;
    }
}

// This function destroys the given arena in a deferred manner
// using g_idle_add() and replaces them with a fresh arena.
//
// The main reason for this is to avoid the use-after-free
// scenario shown in this common case:
//
//     gtk_widget_unparent(root_widget);
//     arena_pop_all(arena);
//
// The call to gtk_widget_unparent() will potentially push callbacks
// into a queue which will run *after* the arena they touch is popped.
Void ui_idle_clear_arena (Mem *root, U64 block_size, Arena **arena) {
    g_idle_add([](Void *data){
        arena_destroy(reinterpret_cast<Arena*>(data));
        return 0;
    }, *arena);

    *arena = arena_new(root, block_size);
}

String ui_rgba_to_string (Mem *mem, GdkRGBA *color) {
    U8 r = static_cast<U8>(color->red   * 255.0 + 0.5);
    U8 g = static_cast<U8>(color->green * 255.0 + 0.5);
    U8 b = static_cast<U8>(color->blue  * 255.0 + 0.5);
    U8 a = static_cast<U8>(color->alpha * 255.0 + 0.5);
    return astr_fmt(mem, "#%02X%02X%02X%02X", r, g, b, a);
}

Void ui_open_web_uri (String uri) {
    tmem_new(tm);
    AString full = astr_new(tm);
    if (! str_starts_with(uri, str("https://"))) astr_push_cstr(&full, "https://");
    astr_push_str(&full, uri);
    astr_push_byte(&full, 0);
    GError *error = 0;
    g_app_info_launch_default_for_uri(full.data, 0, &error);
}

Void ui_open_file_uri (String path) {
    tmem_new(tm);

    AString full = astr_new(tm);

    if (array_get(&path, 0) == '~') {
        astr_push_cstr(&full, g_get_home_dir());
        astr_push_str(&full, str_cut_prefix(path, str("~")));
    } else {
        astr_push_str(&full, path);
    }

    astr_push_byte(&full, 0);

    GError *error = 0;
    g_app_info_launch_default_for_uri(g_filename_to_uri(full.data, 0, &error), 0, &error);
}

Void ui_set_cursor (CString name) {
    GdkSurface *surface = gtk_native_get_surface(gtk_widget_get_native(main_context.window));
    GdkCursor *cursor = gdk_cursor_new_from_name(name, 0);
    gdk_surface_set_cursor(surface, cursor);
    if (cursor) g_object_unref(cursor);
}

Void ui_set_label_font_size (GtkLabel *label, Int size_points) {
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_size_new(size_points * PANGO_SCALE));
    gtk_label_set_attributes(label, attrs);
    pango_attr_list_unref(attrs);
}

Void ui_scroll_to_widget (GtkWidget *w) {
    for (GtkWidget *p = w; p; p = gtk_widget_get_parent(p)) {
        if (GTK_IS_VIEWPORT(p)) {
            gtk_viewport_scroll_to(GTK_VIEWPORT(p), w, 0);
        }
    }
}
