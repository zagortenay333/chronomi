#include "ui/stopwatch.h"
#include "ui/util.h"
#include "ui/widgets.h"
#include "ui/markup.h"
#include "core/config.h"
#include "core/stopwatch.h"
#include "os/fs.h"

struct StopwatchCard {
    Mem *mem;
    UiCard *widget;
    Stopwatch *sw;
    GtkWidget *label;
    UiButton *start_button;
    UiButton *pause_button;
    UiButton *reset_button;
    UiButton *lap_button;
    GtkWidget *laps_box;
};

struct Context {
    Applet pub;
    View *view;
    I64 tic_id;
    StopwatchApplet *core;
    String settings_path;
    Array<StopwatchCard*> cards;
};

static Context context;

static Void main_view_new ();
static Void flush_settings ();
static Void editor_view_new(Stopwatch *);
static Void stopwatch_card_update (StopwatchCard *);
static Void stopwatch_card_update_label (StopwatchCard *);

static Void tic () {
    stopwatch_tic(context.core);

    array_iter (card, &context.cards) {
        if (card->sw->state == STOPWATCH_RUNNING) stopwatch_card_update_label(card);
    }

    Stopwatch *running = array_find_get(&context.core->stopwatches, [](Stopwatch *it){ return it->state == STOPWATCH_RUNNING; });

    if (running) {
        context.tic_id = g_timeout_add(100, [](Void*){ tic(); return 0; }, 0);
    } else {
        context.tic_id = 0;
    }
}

// =============================================================================
// Stopwatch Card
// =============================================================================
static Void stopwatch_card_update_label (StopwatchCard *card) {
    tmem_new(tm);

    U64 cseconds = (card->sw->current / 10) % 100;
    U64 seconds  = (card->sw->current / 1000) % 60;
    U64 minutes  = (card->sw->current / 60000) % 60;
    U64 hours    = card->sw->current / 3600000;
    String label = astr_fmt(tm, "%02lu:%02lu:%02lu.%02lu%c", hours, minutes, seconds, cseconds, 0);

    gtk_label_set_text(GTK_LABEL(card->label), label.data);
}

static Void stopwatch_card_update (StopwatchCard *card) {
    gtk_widget_remove_css_class(card->widget->header, "kronomi-bg-red");

    switch (card->sw->state) {
    case STOPWATCH_RESET:
        gtk_widget_set_visible(card->reset_button->widget, false);
        gtk_widget_set_visible(card->pause_button->widget, false);
        gtk_widget_set_visible(card->start_button->widget, true);
        gtk_widget_set_visible(card->lap_button->widget, false);
        break;
    case STOPWATCH_PAUSED:
        gtk_widget_set_visible(card->reset_button->widget, true);
        gtk_widget_set_visible(card->pause_button->widget, false);
        gtk_widget_set_visible(card->start_button->widget, true);
        gtk_widget_set_visible(card->lap_button->widget, false);
        break;
    case STOPWATCH_RUNNING:
        gtk_widget_set_visible(card->reset_button->widget, true);
        gtk_widget_set_visible(card->pause_button->widget, true);
        gtk_widget_set_visible(card->start_button->widget, false);
        gtk_widget_set_visible(card->lap_button->widget, true);
        gtk_widget_add_css_class(card->widget->header, "kronomi-bg-red");
        break;
    }

    tmem_new(tm);

    GtkWidget *child = gtk_widget_get_first_child(card->laps_box);
    if (child) gtk_widget_unparent(child); // @todo We are leaking this mem until a view change. Good idea?

    if (card->sw->laps.count) {
        gtk_widget_set_visible(card->laps_box, true);
        AString laps = astr_new(tm);
        astr_push_fmt(&laps, "|**%s**\n|**%s**\n|**%s**\n|-\n", tr("Position"), tr("Lap"), tr("Total"));
        array_iter (lap, &card->sw->laps) astr_push_fmt(&laps, "|\\#%lu\n|%lu\n|%lu\n|-\n", ARRAY_IDX+1, lap.lap, lap.total);
        UiMarkup *markup = ui_markup_new(card->mem, astr_to_str(&laps));
        gtk_box_append(GTK_BOX(card->laps_box), markup->widget);
    } else {
        gtk_widget_set_visible(card->laps_box, false);
    }

    stopwatch_card_update_label(card);
}

StopwatchCard *stopwatch_card_new (Mem *mem, Stopwatch *sw) {
    Auto card = mem_new(mem, StopwatchCard);
    card->widget = ui_card_new(mem);
    card->sw = sw;
    card->mem = mem;
    array_push(&context.cards, card);
    defer { stopwatch_card_update(card); };

    //
    // label
    //
    card->label = gtk_label_new(0);
    gtk_box_append(GTK_BOX(card->widget->left_header), card->label);
    if (card->sw->clock_size > 10) ui_set_label_font_size(GTK_LABEL(card->label), card->sw->clock_size);

    //
    // autohide buttons
    //
    UiButton *move_button = ui_button_new(mem, "kronomi-move-top-symbolic", 0, false, false, true);
    gtk_box_append(GTK_BOX(card->widget->autohide_header), move_button->widget);

    UiButton *edit_button = ui_button_new(mem, "kronomi-edit-symbolic", 0, false, false, true);
    gtk_box_append(GTK_BOX(card->widget->autohide_header), edit_button->widget);

    UiPopoverConfirm *delete_button = ui_popover_confirm_new(mem, "kronomi-trash-symbolic", 0, false, false, true);
    gtk_box_append(GTK_BOX(card->widget->autohide_header), delete_button->widget->widget);

    //
    // buttons
    //
    GtkWidget *button_group = ui_button_group_new(false, true);
    gtk_box_append(GTK_BOX(card->widget->body), button_group);

    card->start_button = ui_button_new(context.view->arena, 0, tr("Start"), true, false, false);
    gtk_box_append(GTK_BOX(button_group), card->start_button->widget);

    card->pause_button = ui_button_new(context.view->arena, 0, tr("Pause"), true, false, false);
    gtk_box_append(GTK_BOX(button_group), card->pause_button->widget);

    card->reset_button = ui_button_new(context.view->arena, 0, tr("Reset"), true, false, false);
    gtk_box_append(GTK_BOX(button_group), card->reset_button->widget);

    card->lap_button = ui_button_new(context.view->arena, 0, tr("Lap"), true, false, false);
    gtk_box_append(GTK_BOX(button_group), card->lap_button->widget);

    //
    // laps box
    //
    card->laps_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(card->widget->body), card->laps_box);

    //
    // message
    //
    if (sw->message.count) {
        UiMarkup *msg = ui_markup_new(mem, sw->message);
        gtk_box_append(GTK_BOX(card->widget->body), msg->widget);
    }

    //
    // listen
    //
    Auto scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
    gtk_widget_add_controller(card->label, scroll);
    g_signal_connect(scroll, "scroll", G_CALLBACK(+[](GtkEventControllerScroll *, F64 dx, F64 dy, Void *data){
        Auto card = reinterpret_cast<StopwatchCard*>(data);

        if (dy > 0) {
            card->sw->clock_size -= 10;
        } else {
            card->sw->clock_size += 10;
        }

        if (card->sw->clock_size < 10) card->sw->clock_size = 10;
        ui_set_label_font_size(GTK_LABEL(card->label), card->sw->clock_size);
        flush_settings();

        return true;
    }), card);
    g_signal_connect(move_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto card = reinterpret_cast<StopwatchCard*>(data);
        array_find_remove(&context.core->stopwatches, [&](Stopwatch *it){ return it == card->sw; });
        array_insert(&context.core->stopwatches, card->sw, 0);
        flush_settings();
        main_view_new();
    }), card);
    g_signal_connect(edit_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto card = reinterpret_cast<StopwatchCard*>(data);
        editor_view_new(card->sw);
    }), card);
    g_signal_connect(delete_button->confirm, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto card = reinterpret_cast<StopwatchCard*>(data);
        stopwatch_del(context.core, card->sw);
        flush_settings();
        main_view_new();
    }), card);
    g_signal_connect(card->start_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto card = reinterpret_cast<StopwatchCard*>(data);
        stopwatch_start(context.core, card->sw);
        if (! context.tic_id) tic();
        stopwatch_card_update(card);
    }), card);
    g_signal_connect(card->pause_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto card = reinterpret_cast<StopwatchCard*>(data);
        stopwatch_pause(context.core, card->sw);
        stopwatch_card_update(card);
    }), card);
    g_signal_connect(card->lap_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto card = reinterpret_cast<StopwatchCard*>(data);
        stopwatch_lap(context.core, card->sw);
        stopwatch_card_update(card);
    }), card);
    g_signal_connect(card->reset_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto card = reinterpret_cast<StopwatchCard*>(data);
        stopwatch_reset(context.core, card->sw);
        stopwatch_card_update(card);
    }), card);
    g_signal_connect(card->widget->widget, "destroy", G_CALLBACK(+[](GtkWidget*, Void *data){
        array_find_remove_fast(&context.cards, [&](StopwatchCard *it){ return it == data; });
        array_maybe_decrease_capacity(&context.cards);
    }), card);

    return card;
}

// =============================================================================
// Search View
// =============================================================================
struct ViewSearch {
    View base;
    GtkWidget *entry;
    GtkWidget *delete_checkbox;
    UiScrollbox *searched_cards;
    Array<Stopwatch*> search_results;
};

static Void search_view_do_search () {
    Auto view = reinterpret_cast<ViewSearch*>(context.view);

    if (view->searched_cards) gtk_widget_unparent(view->searched_cards->widget);
    view->search_results.count = 0;

    view->searched_cards = ui_scrollbox_new(context.view->arena, true);
    gtk_box_append(GTK_BOX(view->base.box_right), view->searched_cards->widget);
    gtk_widget_set_hexpand(view->searched_cards->box, true);
    gtk_widget_set_vexpand(view->searched_cards->box, true);
    gtk_widget_set_halign(view->searched_cards->box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(view->searched_cards->box, GTK_ALIGN_START);

    String needle = str(gtk_editable_get_text(GTK_EDITABLE(view->entry)));
    array_iter (sw, &context.core->stopwatches) {
        U64 idx = str_search(needle, sw->message);
        if (idx != ARRAY_NIL_IDX) array_push(&view->search_results, sw);
    }

    array_iter (sw, &view->search_results) {
        StopwatchCard *card = stopwatch_card_new(context.view->arena, sw);
        gtk_box_append(GTK_BOX(view->searched_cards->box), card->widget->widget);
    }
}

static Void search_view_new () {
    if (context.view) ui_view_destroy(context.view);
    Auto view = ui_view_new<ViewSearch>(4*KB, context.pub.roots.box, true, false);
    context.view = &view->base;
    defer { search_view_do_search(); };
    array_init(&view->search_results, context.view->arena);

    //
    // search entry
    //
    view->entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(view->entry), tr("Search stopwatches..."));
    gtk_box_append(GTK_BOX(view->base.box_left), view->entry);

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
    UiRow *row = ui_row_new(context.view->arena, str(tr("Delete selected stopwatches")), view->delete_checkbox);
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
            array_iter (sw, &view->search_results) stopwatch_del(context.core, sw);
            flush_settings();
        }

        main_view_new();
    }), 0);
}

// =============================================================================
// Editor View
// =============================================================================
struct ViewEditor {
    View base;
    Stopwatch *sw;
    UiMarkupEditor2 *editor;
    UiIntPicker *clock_size;
};

static Void editor_view_new (Stopwatch *sw) {
    if (context.view) ui_view_destroy(context.view);
    Auto view = ui_view_new<ViewEditor>(4*KB, context.pub.roots.box, true, true);
    context.view = &view->base;
    view->sw = sw;

    //
    // editor
    //
    view->editor = ui_markup_editor2_new(context.view->arena, sw ? sw->message : str(""), tr("Message"), tr("Message Preview"));
    gtk_box_append(GTK_BOX(view->base.box_left), view->editor->entry_card->widget);
    gtk_box_append(GTK_BOX(view->base.box_right), view->editor->markup_card->widget);

    g_idle_add([](Void *data){
        Auto view = reinterpret_cast<ViewEditor*>(data);
        gtk_widget_grab_focus(view->editor->ed->text_view->entry);
        return 0;
    }, view);

    //
    // clock size
    //
    GtkWidget *group = ui_row_group_new();
    gtk_box_append(GTK_BOX(view->base.box_left), group);

    view->clock_size = ui_int_picker_new(context.view->arena, 0, 1000, 0);
    if (sw) ui_int_picker_set_value(view->clock_size, sw->clock_size);
    UiRow *row = ui_row_new(context.view->arena, str(tr("Clock size")), view->clock_size->widget);
    gtk_box_append(GTK_BOX(group), row->widget);

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
        tmem_new(tm);

        Auto view      = reinterpret_cast<ViewEditor*>(context.view);
        String msg     = ui_text_view_get_text(tm, view->editor->ed->text_view);
        I64 clock_size = view->clock_size->val;

        if (view->sw) {
            stopwatch_edit(context.core, view->sw, clock_size, msg);
        } else {
            stopwatch_add(context.core, clock_size, msg);
        }

        flush_settings();
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
    if (context.view) ui_view_destroy(context.view);
    Auto view = ui_view_new<ViewMain>(4*KB, context.pub.roots.box, false, true);
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

    //
    // stopwatches
    //
    array_iter (sw, &context.core->stopwatches) {
        StopwatchCard *card = stopwatch_card_new(context.view->arena, sw);
        gtk_box_append(GTK_BOX(view->base.box_right), card->widget->widget);
    }

    //
    // listen
    //
    g_signal_connect(add_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void*){ editor_view_new(0); }), 0);
    g_signal_connect(search_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void*){ search_view_new(); }), 0);
}

// =============================================================================
// Applet
// =============================================================================
static Void flush_settings () {
    tmem_new(tm);

    AString a = astr_new(tm);
    astr_push_cstr(&a, "stopwatches = [\n");
    array_iter (sw, &context.core->stopwatches) {
        astr_push_cstr(&a, "    {\n");
        astr_push_fmt(&a, "         message    = \"%.*s\"\n", STR(sw->message));
        astr_push_fmt(&a, "         clock_size = %lu\n", sw->clock_size);
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
    ConfigAst *val = config_get_array(p, p->root, "stopwatches", &error);
    if (! error) {
        array_iter (sw, &val->children) {
            Bool error = false;
            String msg = config_get_string(p, sw, "message", &error);
            U64 clock_size = config_get_u64(p, sw, "clock_size", &error);
            if (! error) stopwatch_add(context.core, clock_size, msg);
        }
    }

    astr_println(&log);
}

Applet *ui_stopwatch_init (AppletRoots *roots) {
    context.pub.roots = *roots;
    context.pub.name = str("Stopwatch");
    context.core = stopwatch_new(&mem_root);

    context.settings_path = astr_fmt(&mem_root, "%.*s/stopwatch.txt", STR(main_context.config_dir_path));
    if (! fs_file_exists(context.settings_path)) fs_write_entire_file(context.settings_path, str("stopwatches = []"));

    array_init(&context.cards, &mem_root);
    load_settings();
    main_view_new();
    return &context.pub;
}
