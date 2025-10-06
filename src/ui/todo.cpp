#include "ui/todo.h"
#include "ui/widgets.h"
#include "ui/markup.h"
#include "ui/util.h"
#include "util/core.h"
#include "core/todo.h"
#include "core/config.h"
#include "os/fs.h"

struct Context {
    Applet pub;
    View *view;
    TodoApplet *core;
    String settings_path;
};

static Context context;

static Void sort_tasks ();
static Void main_view_new ();
static Void flush_settings ();
static Void editor_view_new (Task *);
static Void search_view_new (String);

// =============================================================================
// Task Card
// =============================================================================
struct TaskCard {
    UiCard *widget;
    Task *task;
};

static TaskCard *task_card_new (Mem *mem, Task *task, GtkWidget *body) {
    Auto card = mem_new(mem, TaskCard);
    card->task = task;
    card->widget = ui_card_new(mem);

    //
    // checkbox
    //
    GtkWidget *checkbox = gtk_check_button_new();
    gtk_box_append(GTK_BOX(card->widget->left_header), checkbox);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(checkbox), task->config->flags & MARKUP_AST_META_CONFIG_HAS_DONE);

    //
    // autohide buttons
    //
    UiButton *edit_button = ui_button_new(mem, "kronomi-edit-symbolic", 0, false, false, true);
    gtk_box_append(GTK_BOX(card->widget->autohide_header), edit_button->widget);

    UiPopoverConfirm *delete_button = ui_popover_confirm_new(mem, "kronomi-trash-symbolic", 0, false, false, true);
    gtk_box_append(GTK_BOX(card->widget->autohide_header), delete_button->widget->widget);

    UiButton *pin_button = ui_button_new(mem, "kronomi-pin-symbolic", 0, false, false, true);
    Bool pin = task->config->flags & MARKUP_AST_META_CONFIG_HAS_PIN;
    gtk_box_append(GTK_BOX(pin ? card->widget->header : card->widget->autohide_header), pin_button->widget);

    if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_HIDE) {
        UiButton *hide_button = ui_button_new(mem, "kronomi-hidden-symbolic", 0, false, false, true);
        gtk_box_append(GTK_BOX(card->widget->header), hide_button->widget);
    }

    //
    // priority label
    //
    if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_PRIORITY) {
        tmem_new(tm);
        String l = astr_fmt(tm, "#%lu%c", task->config->priority, 0);
        GtkWidget *label = gtk_label_new(l.data);
        gtk_box_append(GTK_BOX(card->widget->header), label);
        gtk_widget_add_css_class(label, "kronomi-bold");
        gtk_widget_add_css_class(label, "kronomi-warning-color");
    }

    //
    // dates
    //
    if (task->config->flags & (MARKUP_AST_META_CONFIG_HAS_DUE | MARKUP_AST_META_CONFIG_HAS_CREATED | MARKUP_AST_META_CONFIG_HAS_COMPLETED)) {
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, SPACING);
        gtk_box_append(GTK_BOX(card->widget->body), box);

        tmem_new(tm);

        if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_DUE) {
            String label = astr_fmt(tm, "%s: %.*s%c", tr("Due"), STR(task->config->due), 0);
            UiButton *button = ui_button_new(mem, 0, label.data, false, false, false);
            gtk_box_append(GTK_BOX(box), button->widget);
            gtk_widget_add_css_class(button->widget, "kronomi-tag-button");
            gtk_widget_add_css_class(button->widget, "kronomi-error-color");
        }

        if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_CREATED) {
            String label = astr_fmt(tm, "%s: %.*s%c", tr("Created"), STR(task->config->created), 0);
            UiButton *button = ui_button_new(mem, 0, label.data, false, false, false);
            gtk_box_append(GTK_BOX(box), button->widget);
            gtk_widget_add_css_class(button->widget, "kronomi-tag-button");
            gtk_widget_add_css_class(button->widget, "kronomi-success-color");
        }

        if (task->config->flags & MARKUP_AST_META_CONFIG_HAS_COMPLETED) {
            String label = astr_fmt(tm, "%s: %.*s%c", tr("Completed"), STR(task->config->completed), 0);
            UiButton *button = ui_button_new(mem, 0, label.data, false, false, false);
            gtk_box_append(GTK_BOX(box), button->widget);
            gtk_widget_add_css_class(button->widget, "kronomi-tag-button");
            gtk_widget_add_css_class(button->widget, "kronomi-success-color");
        }
    }

    //
    // tags
    //
    if (task->config->tags.count) {
        GtkWidget *tag_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, SPACING);
        gtk_box_append(GTK_BOX(card->widget->body), tag_box);
        U64 count = 0;

        map_iter (slot, &task->config->tags) {
            tmem_new(tm);

            if (count == 5) {
                count = 0;
                tag_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, SPACING);
                gtk_box_append(GTK_BOX(card->widget->body), tag_box);
            }

            String tag = slot->key;
            UiButton *button = ui_button_new(mem, 0, cstr(tm, tag), false, false, false);
            gtk_widget_add_css_class(button->widget, "kronomi-tag-button");
            gtk_widget_add_css_class(button->widget, "kronomi-warning-color");
            gtk_box_append(GTK_BOX(tag_box), button->widget);

            g_signal_connect(button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
                Auto button = reinterpret_cast<UiButton*>(data);
                String tag  = str(gtk_label_get_text(GTK_LABEL(button->label)));
                search_view_new(tag);
            }), button);

            count++;
        }
    }

    //
    // message
    //
    if (body) {
        gtk_box_append(GTK_BOX(card->widget->body), body);
    } else {
        UiMarkup *msg = ui_markup_new(mem, task->text, task->ast);
        msg->on_tag_clicked = [](String tag){ search_view_new(tag); };
        gtk_box_append(GTK_BOX(card->widget->body), msg->widget);
    }

    //
    // listen
    //
    g_signal_connect(checkbox, "toggled", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto card = reinterpret_cast<TaskCard*>(data);
        card->task->config->flags ^= MARKUP_AST_META_CONFIG_HAS_DONE;
        todo_task_update(context.core, card->task);
        sort_tasks();
        todo_flush_file(context.core);
        main_view_new();
    }), card);
    g_signal_connect(edit_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto card = reinterpret_cast<TaskCard*>(data);
        editor_view_new(card->task);
    }), card);
    g_signal_connect(delete_button->confirm, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto card = reinterpret_cast<TaskCard*>(data);
        todo_task_del(context.core, card->task);
        main_view_new();
    }), card);
    g_signal_connect(pin_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto card = reinterpret_cast<TaskCard*>(data);
        card->task->config->flags ^= MARKUP_AST_META_CONFIG_HAS_PIN;
        todo_task_update(context.core, card->task);
        sort_tasks();
        todo_flush_file(context.core);
        main_view_new();
    }), card);

    return card;
}

// =============================================================================
// Kanban
// =============================================================================
struct KanbanColumn {
    Mem *mem;
    U64 task_idx;
    GtkWidget *widget;
    UiLazyScrollbox *scroll;
    MarkupAst *filter;
    String filter_text;
    Array<Task*> tasks;
};

struct Kanban {
    Mem *mem;
    UiScrollbox *widget;
    Array<KanbanColumn*> columns;
};

static KanbanColumn *kanban_column_new (Mem *mem, MarkupAst *filter, String filter_text, Bool show_header) {
    tmem_new(tm);

    Auto col = mem_new(mem, KanbanColumn);
    col->mem = mem;
    col->filter = filter;
    col->filter_text = filter_text;
    col->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, SPACING);
    gtk_widget_set_hexpand(col->widget, false);
    gtk_widget_set_halign(col->widget, GTK_ALIGN_CENTER);

    if (show_header) {
        GtkWidget *header_box = ui_box_new(false);
        gtk_box_append(GTK_BOX(col->widget), header_box);
        gtk_widget_add_css_class(header_box, "kronomi-kanban-header");
        gtk_box_append(GTK_BOX(header_box), gtk_label_new(cstr(tm, col->filter_text)));
    }

    array_init(&col->tasks, mem);
    return col;
}

static Kanban *kanban_new (Mem *mem) {
    tmem_new(tm);

    Auto kanban = mem_new(mem, Kanban);
    kanban->mem = mem;
    kanban->widget = ui_scrollbox_new(mem, false);
    array_init(&kanban->columns, mem);
    gtk_widget_set_hexpand(kanban->widget->widget, false);
    gtk_widget_set_halign(kanban->widget->widget, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(kanban->widget->widget, true);

    //
    // get filters
    //
    Auto filters = array_new<String>(tm);
    Bool has_filters = todo_get_filters(context.core, &filters, str("* & !hide"));

    //
    // make KanbanColumn's
    //
    array_iter (filter, &filters) {
        MarkupAst *filter_node = markup_filter_parse(mem, filter);
        KanbanColumn *col = kanban_column_new(mem, filter_node, filter, has_filters);
        array_push(&kanban->columns, col);
        gtk_box_append(GTK_BOX(kanban->widget->box), col->widget);
    }

    //
    // put filtered tasks into different columns
    //
    array_iter (task, &context.core->tasks) {
        array_iter (column, &kanban->columns) {
            if (todo_passes_filter(task, column->filter)) {
                array_push(&column->tasks, task);
                break;
            }
        }
    }

    //
    // put task widgets into columns
    //
    array_iter (column, &kanban->columns) {
        column->scroll = ui_lazy_scrollbox_new(mem, true, [](Void *data){
            Auto column = reinterpret_cast<KanbanColumn*>(data);
            Task *task = array_try_get(&column->tasks, column->task_idx);

            if (task) {
                column->task_idx++;
                Auto w = task_card_new(column->mem, task, 0);
                return w->widget->widget;
            } else {
                return reinterpret_cast<GtkWidget*>(0);
            }
        }, column);
        gtk_box_append(GTK_BOX(column->widget), column->scroll->widget->widget);
    }

    return kanban;
}

// =============================================================================
// Sort View
// =============================================================================
struct SortRow {
    UiCard *widget;
    UiButton *sort_button;
    Sort sort;
};

struct ViewSort {
    View base;
    UiCard *dragged_row;
    GtkWidget *group;
    Array<SortRow*> sort_rows;
    UiLazyScrollbox *scroll;
    U64 scroll_pos;
};

CString sort_to_string (SortBy sort) {
    switch (sort) {
    case SORT_BY_PIN:      return tr("Sort by pin");
    case SORT_BY_DUE:      return tr("Sort by due");
    case SORT_BY_PRIORITY: return tr("Sort by priority");
    case SORT_BY_DONE:     return tr("Sort by done");
    case SORT_BY_HIDE:     return tr("Sort by hide");
    default:               return 0;
    }
}

// @todo This really should be inside the core module,
// but we need to update the array_sort_cmp function
// first to use qsort_r() instead of qsort()...
static Void sort_tasks () {
    array_sort_cmp(&context.core->tasks, [](Task **a, Task **b){
        return static_cast<Int>(todo_compare_tasks(*a, *b, slice(&context.core->sorts)));
    });
}

static Void sort_view_refresh () {
    Auto view = reinterpret_cast<ViewSort*>(context.view);

    //
    // sort view->sort_rows array.
    //
    U64 idx = 0;
    for (GtkWidget *w = gtk_widget_get_first_child(view->group); w; w = gtk_widget_get_next_sibling(w), idx++) {
        U64 idx2 = array_find(&view->sort_rows, [&](SortRow *it){ return it->widget->widget == w; });
        array_swap(&view->sort_rows, idx, idx2);
    }

    //
    // sort tasks
    //
    context.core->sorts.count = 0;
    array_iter (s, &view->sort_rows) array_push(&context.core->sorts, s->sort);
    sort_tasks();

    //
    // create task cards
    //
    if (view->scroll) gtk_widget_unparent(view->scroll->widget->widget);
    view->scroll_pos = 0;
    view->scroll = ui_lazy_scrollbox_new(context.view->arena, true, [](Void *data){
        Auto view = reinterpret_cast<ViewSort*>(data);
        Task *task = array_try_get(&context.core->tasks, view->scroll_pos++);
        return task ? task_card_new(context.view->arena, task, 0)->widget->widget : reinterpret_cast<GtkWidget*>(0);
    }, view);
    gtk_box_append(GTK_BOX(context.view->box_right), view->scroll->widget->widget);
    gtk_widget_set_hexpand(view->scroll->widget->box, true);
    gtk_widget_set_vexpand(view->scroll->widget->box, true);
    gtk_widget_set_halign(view->scroll->widget->box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(view->scroll->widget->box, GTK_ALIGN_START);
}

static SortRow *sort_row_new (Sort sort) {
    Auto row = mem_new(context.view->arena, SortRow);
    row->sort = sort;

    row->widget = ui_card_new(context.view->arena);
    gtk_widget_set_visible(row->widget->body, false);
    gtk_widget_add_css_class(row->widget->widget, "kronomi-sort-row");

    GtkWidget *label = gtk_label_new(sort_to_string(sort.by));
    gtk_box_append(GTK_BOX(row->widget->left_header), label);

    UiButton *move_down_button = ui_button_new(context.view->arena, "kronomi-pan-down-symbolic", 0, false, false, true);
    gtk_box_append(GTK_BOX(row->widget->autohide_header), move_down_button->widget);

    UiButton *move_up_button = ui_button_new(context.view->arena, "kronomi-pan-up-symbolic", 0, false, false, true);
    gtk_box_append(GTK_BOX(row->widget->autohide_header), move_up_button->widget);

    row->sort_button = ui_button_new(context.view->arena, sort.ascending ? "kronomi-sort-ascending-symbolic" : "kronomi-sort-descending-symbolic", 0, false, false, true);
    gtk_box_append(GTK_BOX(row->widget->header), row->sort_button->widget);

    //
    // listen
    //
    GtkDropTarget *dt = gtk_drop_target_new(G_TYPE_POINTER, GDK_ACTION_MOVE);
    gtk_widget_add_controller(row->widget->widget, GTK_EVENT_CONTROLLER(dt));

    GtkDragSource *src = gtk_drag_source_new();
    gtk_widget_add_controller(row->widget->widget, GTK_EVENT_CONTROLLER(src));
    gtk_drag_source_set_actions(src, GDK_ACTION_MOVE);

    g_signal_connect(move_down_button->widget, "clicked", G_CALLBACK(+[](GtkWidget *, Void *data){
        Auto view = reinterpret_cast<ViewSort*>(context.view);
        Auto row = reinterpret_cast<UiCard*>(data);
        GtkWidget *next = gtk_widget_get_next_sibling(row->widget);
        if (next) gtk_box_reorder_child_after(GTK_BOX(view->group), row->widget, next);
        sort_view_refresh();
    }), row->widget);
    g_signal_connect(move_up_button->widget, "clicked", G_CALLBACK(+[](GtkWidget *, Void *data){
        Auto view = reinterpret_cast<ViewSort*>(context.view);
        Auto row = reinterpret_cast<UiCard*>(data);
        GtkWidget *prev = gtk_widget_get_prev_sibling(row->widget);
        if (prev) gtk_box_reorder_child_after(GTK_BOX(view->group), prev, row->widget);
        sort_view_refresh();
    }), row->widget);
    g_signal_connect(row->sort_button->widget, "clicked", G_CALLBACK(+[](GtkWidget *, Void *data){
        Auto row = reinterpret_cast<SortRow*>(data);
        if (row->sort.ascending) {
            row->sort.ascending = false;
            gtk_image_set_from_icon_name(GTK_IMAGE(row->sort_button->icon), "kronomi-sort-descending-symbolic");
        } else {
            row->sort.ascending = true;
            gtk_image_set_from_icon_name(GTK_IMAGE(row->sort_button->icon), "kronomi-sort-ascending-symbolic");
        }
        sort_view_refresh();
    }), row);
    g_signal_connect(dt, "drop", G_CALLBACK(+[](GtkDropTarget *dt, GValue *v, F64 x, F64 y, Void *data){
        Auto view = reinterpret_cast<ViewSort*>(context.view);
        gtk_widget_set_opacity(view->dragged_row->widget, 1);
        view->dragged_row = 0;
        sort_view_refresh();
    }), 0);
    g_signal_connect(dt, "enter", G_CALLBACK(+[](GtkDropTarget *dt, F64 x, F64 y, Void *data){
        Auto view = reinterpret_cast<ViewSort*>(context.view);
        if (! view->dragged_row) return 0;

        Auto row = reinterpret_cast<UiCard*>(data);
        GtkWidget *prev = gtk_widget_get_prev_sibling(row->widget);
        if (prev == view->dragged_row->widget) {
            gtk_box_reorder_child_after(GTK_BOX(view->group), view->dragged_row->widget, row->widget);
        } else {
            gtk_box_reorder_child_after(GTK_BOX(view->group), view->dragged_row->widget, prev);
        }

        return 0;
    }), row->widget);
    g_signal_connect(src, "drag-begin", G_CALLBACK(+[](GtkDragSource *, GdkDrag *drag, Void *data){
        gtk_drag_icon_set_from_paintable(drag, NULL, 0, 0);
    }), 0);
    g_signal_connect(src, "prepare", G_CALLBACK(+[](GtkDragSource *, F64 x, F64 y, Void *data){
        Auto view = reinterpret_cast<ViewSort*>(context.view);
        Auto row = reinterpret_cast<UiCard*>(data);
        view->dragged_row = row;
        gtk_widget_set_opacity(row->widget, 0.4);
        GValue v = G_VALUE_INIT;
        g_value_init (&v, G_TYPE_POINTER);
        g_value_set_pointer (&v, row);
        GdkContentProvider *prov = gdk_content_provider_new_for_value(&v);
        g_value_unset (&v);
        return prov;
    }), row->widget);

    return row;
}

static Void sort_view_new () {
    if (context.view) ui_view_destroy(context.view);
    Auto view = ui_view_new<ViewSort>(4*KB, context.pub.roots.box, true, false);
    context.view = &view->base;
    array_init(&view->sort_rows, context.view->arena);
    defer { sort_view_refresh(); };

    view->group = ui_row_group_new();
    gtk_box_append(GTK_BOX(view->base.box_left), view->group);

    array_iter (sort, &context.core->sorts) {
        SortRow *row = sort_row_new(sort);
        array_push(&view->sort_rows, row);
        gtk_box_append(GTK_BOX(view->group), row->widget->widget);
    }

    //
    // buttons
    //
    GtkWidget *button_group = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, SPACING);
    gtk_box_append(GTK_BOX(view->base.box_left), button_group);

    UiButton *ok_button = ui_button_new(context.view->arena, 0, tr("Ok"), true, false, false);
    gtk_box_append(GTK_BOX(button_group), ok_button->widget);

    UiPopover *help_button = ui_popover_new(context.view->arena, "kronomi-question-symbolic", 0, false, false, false);
    gtk_box_append(GTK_BOX(button_group), help_button->widget);
    gtk_widget_grab_focus(help_button->widget);
    ui_popover_set_info(help_button, context.view->arena, str(tr(
        "Tasks are sorted by the first attribute in this list.\n"
        "In case of a tie, by the second attribute and so on...\n"
        "[tip] You can drag-and-drop the items in this list."
    )));

    //
    // listen
    //
    g_signal_connect(ok_button->widget, "clicked", G_CALLBACK(+[](GtkWidget *, Void *data){
        flush_settings();
        main_view_new();
    }), 0);
}

// =============================================================================
// Deck View
// =============================================================================
struct ViewDeck;

struct DeckWidget {
    UiCard *widget;
    ViewDeck *view;
    Deck *deck;
    GtkWidget *checkbox;
    Bool ignore_checkbox_toggle;
    UiFilePicker *path;
    GtkWidget *filters;
};

struct ViewDeck {
    View base;
    GtkWidget *entry;
    GtkWidget *delete_checkbox;
    Array<DeckWidget*> deck_widgets;
};

static DeckWidget *deck_widget_new (ViewDeck *view, Deck *deck) {
    tmem_new(tm);

    Auto deck_widget = mem_new(context.view->arena, DeckWidget);
    deck_widget->view = view;
    deck_widget->deck = deck;
    deck_widget->widget = ui_card_new(context.view->arena);
    array_push(&view->deck_widgets, deck_widget);

    //
    // checkbox
    //
    deck_widget->checkbox = gtk_check_button_new();
    gtk_box_append(GTK_BOX(deck_widget->widget->left_header), deck_widget->checkbox);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(deck_widget->checkbox), deck ? deck->active : false);

    //
    // autohide buttons
    //
    UiPopoverConfirm *delete_button = ui_popover_confirm_new(context.view->arena, "kronomi-trash-symbolic", 0, false, false, true);
    gtk_box_append(GTK_BOX(deck_widget->widget->autohide_header), delete_button->widget->widget);

    //
    // file picker
    //
    deck_widget->path = ui_file_picker_new(context.view->arena, 0, false, false, {});
    gtk_box_append(GTK_BOX(deck_widget->widget->body), deck_widget->path->widget);
    gtk_widget_set_hexpand(deck_widget->path->entry, true);
    if (deck) {
        gtk_editable_set_text(GTK_EDITABLE(deck_widget->path->entry), cstr(tm, deck->path));
        gtk_editable_set_position(GTK_EDITABLE(deck_widget->path->entry), -1);
    }

    //
    // filters
    //
    deck_widget->filters = gtk_entry_new();
    gtk_box_append(GTK_BOX(deck_widget->widget->body), deck_widget->filters);
    gtk_entry_set_placeholder_text(GTK_ENTRY(deck_widget->filters), tr("Comma separated list of filter expressions..."));
    if (deck) gtk_editable_set_text(GTK_EDITABLE(deck_widget->filters), cstr(tm, deck->filters));

    //
    // listen
    //
    g_signal_connect(delete_button->confirm, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto w = reinterpret_cast<DeckWidget*>(data);
        array_find_remove(&w->view->deck_widgets, [&](DeckWidget *it){ return it == w; });
        gtk_widget_unparent(w->widget->widget);
    }), deck_widget);
    g_signal_connect(deck_widget->checkbox, "toggled", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto deck_widget = reinterpret_cast<DeckWidget*>(data);
        if (deck_widget->ignore_checkbox_toggle) return;
        array_iter (w, &deck_widget->view->deck_widgets) {
            w->ignore_checkbox_toggle = true;
            gtk_check_button_set_active(GTK_CHECK_BUTTON(w->checkbox), w == deck_widget);
            w->ignore_checkbox_toggle = false;
        }
    }), deck_widget);

    return deck_widget;
}

static Void deck_view_do_search () {
    Auto view = reinterpret_cast<ViewDeck*>(context.view);

    String needle = str(gtk_editable_get_text(GTK_EDITABLE(view->entry)));

    array_iter (deck_widget, &view->deck_widgets) {
        String haystack = str(gtk_editable_get_text(GTK_EDITABLE(deck_widget->path->entry)));
        U64 idx = str_search(needle, haystack);
        gtk_widget_set_visible(deck_widget->widget->widget, idx != ARRAY_NIL_IDX);
    }
}

static Void deck_view_new () {
    tmem_new(tm);

    if (context.view) ui_view_destroy(context.view);
    Auto view = ui_view_new<ViewDeck>(4*KB, context.pub.roots.box, true, true);
    context.view = &view->base;
    array_init(&view->deck_widgets, context.view->arena);
    defer { deck_view_do_search(); };

    //
    // search entry
    //
    view->entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(view->entry), tr("Search decks..."));
    gtk_box_append(GTK_BOX(view->base.box_left), view->entry);

    g_idle_add([](Void *data){
        Auto view = reinterpret_cast<ViewDeck*>(data);
        gtk_widget_grab_focus(view->entry);
        return 0;
    }, view);

    //
    // delete row
    //
    GtkWidget *group = ui_row_group_new();
    gtk_box_append(GTK_BOX(view->base.box_left), group);

    view->delete_checkbox = gtk_check_button_new();
    UiRow *row = ui_row_new(context.view->arena, str(tr("Remove selected decks")), view->delete_checkbox);
    gtk_box_append(GTK_BOX(group), row->widget);
    gtk_widget_add_css_class(row->label, "kronomi-error-color");

    //
    // buttons
    //
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, SPACING);
    gtk_box_append(GTK_BOX(view->base.box_left), box);

    GtkWidget *button_group = ui_button_group_new(false, true);
    gtk_box_append(GTK_BOX(box), button_group);

    UiButton *close_button = ui_button_new(context.view->arena, 0, tr("Close"), true, false, false);
    gtk_box_append(GTK_BOX(button_group), close_button->widget);

    UiPopoverConfirm *apply_button = ui_popover_confirm_new(context.view->arena, 0, tr("Apply"), true, false, false);
    gtk_box_append(GTK_BOX(button_group), apply_button->widget->widget);

    UiButton *add_button = ui_button_new(context.view->arena, 0, tr("Add Deck"), true, false, false);
    gtk_box_append(GTK_BOX(button_group), add_button->widget);

    UiPopover *help_button = ui_popover_new(context.view->arena, "kronomi-question-symbolic", 0, false, false, false);
    gtk_box_append(GTK_BOX(box), help_button->widget);

    AString help_text = astr_new(context.view->arena);
    astr_push_str(&help_text, fs_read_entire_file(tm, str("data/docs/deck_view_help"), 0));
    astr_push_str(&help_text, fs_read_entire_file(tm, str("data/docs/filters"), 0));
    ui_popover_set_info(help_button, context.view->arena, astr_to_str(&help_text));

    //
    // deck_widgets
    //
    array_iter (deck, &context.core->decks) {
        DeckWidget *w = deck_widget_new(view, deck);
        gtk_box_append(GTK_BOX(view->base.box_right), w->widget->widget);
    }

    //
    // listen
    //
    g_signal_connect(GTK_EDITABLE(view->entry), "changed", G_CALLBACK(+[](GtkEditable*, Void *){ deck_view_do_search(); }), 0);
    g_signal_connect(close_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){ main_view_new(); }), 0);
    g_signal_connect(add_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto view = reinterpret_cast<ViewDeck*>(context.view);
        DeckWidget *card = deck_widget_new(view, 0);
        gtk_box_append(GTK_BOX(view->base.box_right), card->widget->widget);
    }), 0);
    g_signal_connect(apply_button->confirm, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto view = reinterpret_cast<ViewDeck*>(context.view);

        array_iter (w, &view->deck_widgets) {
            Bool active    = ui_checkbox_checked(w->checkbox);
            String path    = str(gtk_editable_get_text(GTK_EDITABLE(w->path->entry)));
            String filters = str(gtk_editable_get_text(GTK_EDITABLE(w->filters)));

            if (! w->deck) {
                todo_deck_add(context.core, active, path, filters);
            } else if ((w->deck->active != active) || !str_match(w->deck->path, path) || !str_match(w->deck->filters, filters)) {
                todo_deck_del(context.core, w->deck);
                todo_deck_add(context.core, active, path, filters);
            }
        }

        flush_settings();
        todo_load_file(context.core);
        sort_tasks();
        main_view_new();
    }), 0);
}

// =============================================================================
// Search View
// =============================================================================
struct ViewSearch {
    View base;
    GtkWidget *entry;
    GtkWidget *delete_checkbox;
    UiPopover *entry_help_button;
    UiLazyScrollbox *searched_cards;
    Array<Task*> search_results;
    U64 scroll_pos;
};

static Void search_view_do_search () {
    tmem_new(tm);

    Auto view = reinterpret_cast<ViewSearch*>(context.view);

    if (view->searched_cards) gtk_widget_unparent(view->searched_cards->widget->widget);
    view->scroll_pos = 0;
    view->search_results.count = 0;

    String needle = str(gtk_editable_get_text(GTK_EDITABLE(view->entry)));
    MarkupAst *filter = markup_filter_parse(tm, needle);

    array_iter (task, &context.core->tasks) {
        if (todo_passes_filter(task, filter)) array_push(&view->search_results, task);
    }

    view->searched_cards = ui_lazy_scrollbox_new(context.view->arena, true, [](Void *){
        Auto view = reinterpret_cast<ViewSearch*>(context.view);
        Task *task = array_try_get(&view->search_results, view->scroll_pos++);
        return task ? task_card_new(context.view->arena, task, 0)->widget->widget : reinterpret_cast<GtkWidget*>(0);
    }, 0);
    gtk_box_append(GTK_BOX(view->base.box_right), view->searched_cards->widget->widget);
    gtk_widget_set_hexpand(view->searched_cards->widget->box, true);
    gtk_widget_set_vexpand(view->searched_cards->widget->box, true);
    gtk_widget_set_halign(view->searched_cards->widget->box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(view->searched_cards->widget->box, GTK_ALIGN_START);
}

static Void search_view_new (String needle) {
    tmem_new(tm);

    if (context.view) ui_view_destroy(context.view);
    Auto view = ui_view_new<ViewSearch>(4*KB, context.pub.roots.box, true, false);
    context.view = &view->base;
    defer { search_view_do_search(); };
    array_init(&view->search_results, context.view->arena);

    //
    // search entry
    //
    GtkWidget *search_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append(GTK_BOX(view->base.box_left), search_box);
    gtk_widget_add_css_class(search_box, "kronomi-file-picker");

    view->entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(view->entry), tr("Filter expression..."));
    gtk_box_append(GTK_BOX(search_box), view->entry);
    gtk_widget_set_hexpand(view->entry, true);
    gtk_editable_set_text(GTK_EDITABLE(view->entry), cstr(tm, needle));

    view->entry_help_button = ui_popover_new(context.view->arena, "kronomi-question-symbolic", 0, false, false, false);
    gtk_box_append(GTK_BOX(search_box), view->entry_help_button->widget);
    ui_popover_set_info(view->entry_help_button, context.view->arena, fs_read_entire_file(tm, str("data/docs/filters"), 0));

    g_idle_add([](Void *data){
        Auto view = reinterpret_cast<ViewSearch*>(data);
        gtk_widget_grab_focus(view->entry);
        return 0;
    }, view);

    //
    // delete row
    //
    GtkWidget *group = ui_row_group_new();
    gtk_box_append(GTK_BOX(view->base.box_left), group);

    view->delete_checkbox = gtk_check_button_new();
    UiRow *row = ui_row_new(context.view->arena, str(tr("Delete selected tasks")), view->delete_checkbox);
    gtk_box_append(GTK_BOX(group), row->widget);
    gtk_widget_add_css_class(row->label, "kronomi-error-color");

    //
    // buttons
    //
    GtkWidget *button_group = ui_button_group_new(false, true);
    gtk_box_append(GTK_BOX(view->base.box_left), button_group);

    UiButton *close_button = ui_button_new(context.view->arena, 0, tr("Close"), true, false, false);
    gtk_box_append(GTK_BOX(button_group), close_button->widget);

    UiPopoverConfirm *apply_button = ui_popover_confirm_new(context.view->arena, 0, tr("Apply"), true, false, false);
    gtk_box_append(GTK_BOX(button_group), apply_button->widget->widget);

    //
    // listen
    //
    g_signal_connect(GTK_EDITABLE(view->entry), "changed", G_CALLBACK(+[](GtkEditable*, Void *){ search_view_do_search(); }), 0);
    g_signal_connect(close_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){ main_view_new(); }), 0);
    g_signal_connect(apply_button->confirm, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto view = reinterpret_cast<ViewSearch*>(context.view);

        if (ui_checkbox_checked(view->delete_checkbox)) {
            array_iter (task, &view->search_results) todo_task_del(context.core, task);
            todo_flush_file(context.core);
        }

        main_view_new();
    }), 0);
}

// =============================================================================
// Editor View
// =============================================================================
struct ViewEditor {
    View base;
    Task *task;
    UiMarkupEditor *editor;
};

static Void editor_view_wrap_preview_nodes (UiMarkupEditor *ed) {
    array_iter (node, &ed->markup->ast->children) {
        GtkWidget *widget = map_get_assert(&ed->markup->ast_to_widget, node);
        GtkWidget *parent = gtk_widget_get_parent(widget);
        GtkWidget *prev   = gtk_widget_get_prev_sibling(widget);

        assert_always(GTK_IS_BOX(parent));

        g_object_ref(widget);
        defer { g_object_unref(widget); };
        gtk_widget_unparent(widget);

        if (todo_is_valid_task(context.core, node)) {
            Auto dummy_task = mem_new(&ed->markup_arena->base, Task);
            dummy_task->text = ed->markup->text;
            dummy_task->ast = node;
            dummy_task->config = reinterpret_cast<MarkupAstMeta*>(node)->config;

            TaskCard *card = task_card_new(&ed->markup_arena->base, dummy_task, widget);
            gtk_box_insert_child_after(GTK_BOX(parent), card->widget->widget, prev);
            gtk_widget_add_css_class(card->widget->widget, "block");
        } else {
            UiCard *card = ui_card_new(&ed->markup_arena->base);
            gtk_box_insert_child_after(GTK_BOX(parent), card->widget, prev);
            gtk_widget_add_css_class(card->header, "kronomi-bg-error");
            gtk_widget_add_css_class(card->widget, "block");
            gtk_box_append(GTK_BOX(card->body), widget);

            GtkWidget *label = gtk_label_new(tr("Invalid task"));
            gtk_box_append(GTK_BOX(card->left_header), label);
        }
    }
}

static Void editor_view_new (Task *task) {
    tmem_new(tm);

    if (context.view) ui_view_destroy(context.view);
    Auto view = ui_view_new<ViewEditor>(4*KB, context.pub.roots.box, true, true);
    context.view = &view->base;
    view->task = task;

    //
    // editor
    //
    String init_text;
    if (task) {
        init_text = markup_ast_get_text(task->ast, task->text);
    } else {
        DateTime d = os_get_date_time();
        init_text = astr_fmt(tm, "[created: %u-%02u-%02u] ", d.year, d.month, d.day);
    }

    view->editor = ui_markup_editor_new(context.view->arena, init_text, editor_view_wrap_preview_nodes);
    gtk_box_append(GTK_BOX(view->base.box_right), view->editor->markup_scroll->widget);

    UiCard *entry = ui_card_new(context.view->arena);
    gtk_box_append(GTK_BOX(entry->body), view->editor->text_view->widget->widget);
    gtk_widget_add_css_class(entry->widget, "kronomi-entry-card");
    gtk_box_append(GTK_BOX(entry->left_header), gtk_label_new(tr("Message")));
    gtk_box_append(GTK_BOX(view->base.box_left), entry->widget);

    g_idle_add([](Void *data){
        Auto view = reinterpret_cast<ViewEditor*>(data);
        gtk_widget_grab_focus(view->editor->text_view->entry);
        return 0;
    }, view);

    //
    // buttons
    //
    GtkWidget *button_group = ui_button_group_new(false, true);
    gtk_box_append(GTK_BOX(view->base.box_left), button_group);

    UiButton *close_button = ui_button_new(context.view->arena, 0, tr("Close"), true, false, false);
    gtk_box_append(GTK_BOX(button_group), close_button->widget);

    UiButton *ok_button = ui_button_new(context.view->arena, 0, tr("Ok"), true, false, false);
    gtk_box_append(GTK_BOX(button_group), ok_button->widget);

    //
    // listen
    //
    g_signal_connect(close_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){ main_view_new(); }), 0);
    g_signal_connect(ok_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto view  = reinterpret_cast<ViewEditor*>(context.view);
        String msg = ui_text_view_get_text(&context.core->deck_arena->base, view->editor->text_view);

        if (view->task) todo_task_del(context.core, view->task);
        todo_tasks_add(context.core, msg, false);

        todo_flush_file(context.core);
        main_view_new();
    }), 0);
}

// =============================================================================
// Main View
// =============================================================================
struct ViewMain {
    View base;
};

static Void main_view_new () {
    if (! todo_get_active_deck(context.core)) {
        deck_view_new();
        return;
    }

    if (context.core->fragmentation > 100) {
        todo_load_file(context.core);
    }

    if (context.view) ui_view_destroy(context.view);
    Auto view = ui_view_new<ViewMain>(4*KB, context.pub.roots.box, false, false);
    context.view = &view->base;

    //
    // floating buttons
    //
    GtkWidget *floating_buttons = ui_button_group_new(true, false);
    gtk_box_append(GTK_BOX(view->base.box_left), floating_buttons);

    UiButton *add_button = ui_button_new(context.view->arena, "kronomi-plus-symbolic", 0, false, false, false);
    gtk_box_append(GTK_BOX(floating_buttons), add_button->widget);

    UiButton *search_button = ui_button_new(context.view->arena, "kronomi-search-symbolic", 0, false, false, false);
    gtk_box_append(GTK_BOX(floating_buttons), search_button->widget);

    UiButton *deck_button = ui_button_new(context.view->arena, "kronomi-folder-symbolic", 0, false, false, false);
    gtk_box_append(GTK_BOX(floating_buttons), deck_button->widget);

    UiButton *sort_button = ui_button_new(context.view->arena, "kronomi-sort-ascending-symbolic", 0, false, false, false);
    gtk_box_append(GTK_BOX(floating_buttons), sort_button->widget);

    //
    // cards
    //
    Kanban *kanban = kanban_new(context.view->arena);
    gtk_box_append(GTK_BOX(view->base.box_right), kanban->widget->widget);

    //
    // listen
    //
    g_signal_connect(add_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void*){ editor_view_new(0); }), 0);
    g_signal_connect(search_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void*){ search_view_new({}); }), 0);
    g_signal_connect(deck_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void*){ deck_view_new(); }), 0);
    g_signal_connect(sort_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void*){ sort_view_new(); }), 0);
}

// =============================================================================
// Applet
// =============================================================================
static Void flush_settings () {
    tmem_new(tm);
    AString a = astr_new(tm);

    astr_push_cstr(&a, "sort = [\n");
    array_iter (sort, &context.core->sorts) {
        astr_push_cstr(&a, "    {\n");
        astr_push_fmt(&a, "         ascending = %s\n", sort.ascending ? "true" : "false");
        astr_push_fmt(&a, "         by        = %lu\n", static_cast<U64>(sort.by));
        astr_push_cstr(&a, "    }\n");
    }
    astr_push_cstr(&a, "]\n");

    astr_push_cstr(&a, "decks = [\n");
    array_iter (deck, &context.core->decks) {
        astr_push_cstr(&a, "    {\n");
        astr_push_fmt(&a, "         active        = %s\n", deck->active ? "true" : "false");
        astr_push_fmt(&a, "         path          = \"%.*s\"\n", STR(deck->path));
        astr_push_fmt(&a, "         filters       = \"%.*s\"\n", STR(deck->filters));
        astr_push_cstr(&a, "    }\n");
    }
    astr_push_cstr(&a, "]\n");

    fs_write_entire_file(context.settings_path, astr_to_str(&a));
}

static Void load_settings () {
    tmem_new(tm);

    AString log = astr_new(tm);
    ConfigParser *p = config_load(tm, context.settings_path, &log);

    Bool error = false;
    ConfigAst *decks = config_get_array(p, p->root, "decks", &error);
    if (! error) {
        array_iter (deck, &decks->children) {
            Bool active    = config_get_bool(p, deck, "active", &error);
            String path    = config_get_string(p, deck, "path", &error);
            String filters = config_get_string(p, deck, "filters", &error);
            if (! error) todo_deck_add(context.core, active, path, filters);
        }
    }

    error = false;
    ConfigAst *sorts = config_get_array(p, p->root, "sort", &error);
    if (! error) {
        array_iter (sort, &sorts->children) {
            Bool ascending = config_get_bool(p, sort, "ascending", &error);
            U64 by = config_get_u64(p, sort, "by", &error);
            if (! error) array_push_lit(&context.core->sorts, .ascending=ascending, .by=static_cast<SortBy>(by));
        }
    }

    todo_load_file(context.core);
    sort_tasks();
    astr_println(&log);
}

Applet *ui_todo_init (AppletRoots *roots) {
    context.pub.roots = *roots;
    context.pub.name = str("Todo");

    context.settings_path = astr_fmt(&mem_root, "%.*s/todo.txt", STR(main_context.config_dir_path));
    if (! fs_file_exists(context.settings_path)) {
        fs_write_entire_file(context.settings_path, str(
            "sort = [\n"
            "    {\n"
            "         ascending = false\n"
            "         by        = 0\n"
            "    }\n"
            "    {\n"
            "         ascending = false\n"
            "         by        = 3\n"
            "    }\n"
            "    {\n"
            "         ascending = true\n"
            "         by        = 1\n"
            "    }\n"
            "    {\n"
            "         ascending = true\n"
            "         by        = 2\n"
            "    }\n"
            "    {\n"
            "         ascending = false\n"
            "         by        = 4\n"
            "    }\n"
            "]\n"
            "decks = []"
        ));
    }

    context.core = todo_new(&mem_root);
    load_settings();
    main_view_new();
    return &context.pub;
}
