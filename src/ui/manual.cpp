#include "ui/manual.h"
#include "ui/widgets.h"
#include "ui/markup.h"
#include "ui/util.h"
#include "util/core.h"
#include "core/config.h"
#include "os/fs.h"

struct Context {
    Applet applet;
    View *view;
};

static Context context;

// =============================================================================
// Main View
// =============================================================================
struct ViewMain {
    View base;
    UiScrollbox *toc;
    UiMarkupEditor *editor;
    GtkWidget *selected_button;
    Arena *toc_arena;
};

static Void render_table_of_contents () {
    Auto view = reinterpret_cast<ViewMain*>(context.view);

    if (view->toc) gtk_widget_unparent(view->toc->widget);
    arena_pop_all(view->toc_arena);

    view->toc = ui_scrollbox_new(&view->toc_arena->base, true);
    gtk_box_append(GTK_BOX(context.view->box_left), view->toc->widget);
    gtk_box_set_spacing(GTK_BOX(view->toc->box), 0);

    array_iter (node, &view->editor->markup->ast->children) {
        tmem_new(tm);

        if (node->tag != MARKUP_AST_HEADER) continue;

        U64 size = reinterpret_cast<MarkupAstHeader*>(node)->size;
        String text = markup_ast_get_text(array_get(&node->children, 0), view->editor->markup->text);
        if (array_get_last(&text) == '\n') text.count--;

        AString a = astr_new(tm);
        astr_push_fmt(&a, "%*s", 4 * static_cast<Int>(size - 1), "");
        astr_push_str(&a, text);
        astr_push_byte(&a, 0);

        UiButton *button = ui_button_new(&view->toc_arena->base, 0, a.data, true, false, true);
        gtk_widget_set_halign(button->label, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(view->toc->box), button->widget);
        gtk_widget_add_css_class(button->widget, "kronomi-toc-button");

        GtkWidget *header_widget = map_get_ptr(&view->editor->markup->ast_to_widget, node);

        g_signal_connect(button->widget, "clicked", G_CALLBACK(+[](GtkWidget *button, Void *data){
            Auto view   = reinterpret_cast<ViewMain*>(context.view);
            Auto header = reinterpret_cast<GtkWidget*>(data);

            if (view->selected_button) gtk_widget_remove_css_class(view->selected_button, "active");
            gtk_widget_add_css_class(button, "active");
            view->selected_button = button;

            Auto adjust = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(view->editor->markup_scroll->widget));
            gtk_adjustment_set_value(adjust, UINT64_MAX);

            g_timeout_add(100, [](Void *data){
                Auto view = reinterpret_cast<ViewMain*>(context.view);
                Auto header = reinterpret_cast<GtkWidget*>(data);
                gtk_viewport_scroll_to(GTK_VIEWPORT(view->editor->markup_scroll->viewport), header, 0);
                return 0;
            }, header);

        }), header_widget);
    }
}

static Void main_view_new () {
    if (context.view) ui_view_destroy(context.view);
    Auto view = ui_view_new<ViewMain>(4*KB, context.applet.roots.box, true, false);
    context.view = &view->base;
    view->toc_arena = arena_new(&mem_root, 512);

    //
    // editor
    //
    String file = fs_read_entire_file(context.view->arena, str("data/docs/markup"), 0);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, SPACING);
    gtk_box_append(GTK_BOX(context.view->box_right), box);
    gtk_widget_set_hexpand(box, true);

    GtkWidget *editor_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append(GTK_BOX(box), editor_box);
    gtk_widget_add_css_class(editor_box, "kronomi-padding");

    view->editor = ui_markup_editor_new(context.view->arena, file);
    gtk_box_append(GTK_BOX(editor_box), view->editor->text_view->widget->widget);
    gtk_box_append(GTK_BOX(box), view->editor->markup_scroll->widget);
    gtk_widget_set_size_request(view->editor->text_view->widget->widget, 450, -1);
    view->editor->preview_reactive = true;

    gtk_widget_set_visible(view->editor->text_view->widget->widget, false);
    gtk_widget_set_hexpand(view->editor->markup_scroll->box, true);
    gtk_widget_set_halign(view->editor->markup_scroll->box, GTK_ALIGN_CENTER);
    gtk_widget_add_css_class(view->editor->markup_scroll->box, "kronomi-padding");

    //
    // disable/enable editor
    //
    GtkWidget *toggle = ui_switch_new(false);
    UiRow *row = ui_row_new(context.view->arena, str(tr("Enable editor")), toggle);
    gtk_box_append(GTK_BOX(view->base.box_left), row->widget);
    gtk_widget_add_css_class(row->widget, "kronomi-toc-editor-enable");
    gtk_widget_remove_css_class(row->widget, "kronomi-row");
    g_signal_connect(toggle, "state-set", G_CALLBACK(+[](GtkSwitch *, Bool state, Void *data){
        Auto view = reinterpret_cast<ViewMain*>(context.view);
        gtk_widget_set_visible(view->editor->text_view->widget->widget, state);
    }), 0);

    //
    // table of contents
    //
    view->editor->on_render = [](UiMarkupEditor *e){ render_table_of_contents(); };
    render_table_of_contents();
}

// =============================================================================
// Applet
// =============================================================================
Applet *ui_manual_init (AppletRoots *roots) {
    context.applet.roots = *roots;
    context.applet.name = str("Manual");
    main_view_new();
    return &context.applet;
}
