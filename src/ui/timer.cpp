#include "ui/timer.h"
#include "ui/widgets.h"
#include "ui/markup.h"
#include "ui/util.h"
#include "util/core.h"
#include "core/timer.h"
#include "core/config.h"
#include "os/fs.h"

struct TimerCard {
    UiCard *widget;
    Timer *timer;
    GtkWidget *label;
    UiButton *start_button;
    UiButton *pause_button;
    UiButton *reset_button;
    GtkMediaStream *sound;
};

struct Context {
    Applet pub;
    View *view;
    I64 tic_id;
    TimerApplet *core;
    String settings_path;
    Array<TimerCard*> cards;
};

static Context context;

static Void main_view_new ();
static Void notif_view_new ();
static Void flush_settings ();
static Void editor_view_new (Timer *);
static Void timer_card_update (TimerCard *);
static Void timer_card_update_label (TimerCard *);

static Void tic () {
    Bool notif = timer_tic(context.core);

    if (notif) {
        flush_settings(); // Because timers that ran out were moved to the start.
        main_view_new();
    } else {
        array_iter (card, &context.cards) {
            if (card->timer->state == TIMER_RUNNING) timer_card_update_label(card);
        }
    }

    Timer *running = array_find_get(&context.core->timers, [](Timer *it){ return it->state == TIMER_RUNNING; });
    context.tic_id = running ? g_timeout_add_seconds(1, [](Void*){ tic(); return 0; }, 0) : 0;
}

// =============================================================================
// Timer Card
// =============================================================================
static Void timer_card_update_label (TimerCard *card) {
    tmem_new(tm);

    U64 seconds  = (card->timer->remaining / 1000) % 60;
    U64 minutes  = (card->timer->remaining / 60000) % 60;
    U64 hours    = card->timer->remaining / 3600000;
    String label = astr_fmt(tm, "%02lu:%02lu:%02lu%c", hours, minutes, seconds, 0);

    gtk_label_set_text(GTK_LABEL(card->label), label.data);
}

static Void timer_card_update (TimerCard *card) {
    gtk_widget_remove_css_class(card->widget->header, "kronomi-bg-warning");
    gtk_widget_remove_css_class(card->widget->body, "kronomi-bg-error");
    gtk_widget_set_visible(card->reset_button->widget, false);
    gtk_widget_set_visible(card->pause_button->widget, false);
    gtk_widget_set_visible(card->start_button->widget, false);
    gtk_widget_set_visible(card->widget->header, true);

    if (card->sound) {
        gtk_media_stream_pause(card->sound);
        g_object_unref(card->sound);
        card->sound = 0;
    }

    switch (card->timer->state) {
    case TIMER_RESET:
        gtk_widget_set_visible(card->start_button->widget, true);
        break;
    case TIMER_PAUSED:
        gtk_widget_set_visible(card->reset_button->widget, true);
        gtk_widget_set_visible(card->start_button->widget, true);
        break;
    case TIMER_RUNNING:
        gtk_widget_add_css_class(card->widget->header, "kronomi-bg-warning");
        gtk_widget_set_visible(card->reset_button->widget, true);
        gtk_widget_set_visible(card->pause_button->widget, true);
        break;
    case TIMER_NOTIF:
        if (!card->sound && card->timer->sound.data) {
            tmem_new(tm);
            card->sound = gtk_media_file_new_for_filename(cstr(tm, card->timer->sound));
            gtk_media_stream_set_loop(card->sound, true);
            gtk_media_stream_play(card->sound);
        }
        gtk_widget_add_css_class(card->widget->body, "kronomi-bg-error");
        gtk_widget_set_visible(card->reset_button->widget, true);
        gtk_widget_set_visible(card->widget->header, false);
        break;
    }

    timer_card_update_label(card);
}

static TimerCard *timer_card_new (Mem *mem, Timer *timer) {
    Auto card = mem_new(mem, TimerCard);
    card->widget = ui_card_new(mem);
    card->timer = timer;
    array_push(&context.cards, card);
    defer { timer_card_update(card); };

    //
    // label
    //
    card->label = gtk_label_new(0);
    gtk_box_append(GTK_BOX(card->widget->left_header), card->label);
    if (card->timer->clock_size > 10) ui_set_label_font_size(GTK_LABEL(card->label), card->timer->clock_size);

    //
    // autohide buttons
    //
    UiButton *edit_button = ui_button_new(mem, "kronomi-edit-symbolic", 0, false, false, true);
    gtk_box_append(GTK_BOX(card->widget->autohide_header), edit_button->widget);

    UiPopoverConfirm *delete_button = ui_popover_confirm_new(mem, "kronomi-trash-symbolic", 0, false, false, true);
    gtk_box_append(GTK_BOX(card->widget->autohide_header), delete_button->widget->widget);

    //
    // timer buttons
    //
    GtkWidget *button_group = ui_button_group_new(false, true);
    gtk_box_append(GTK_BOX(card->widget->body), button_group);

    card->start_button = ui_button_new(context.view->arena, 0, tr("Start"), true, false, false);
    gtk_box_append(GTK_BOX(button_group), card->start_button->widget);

    card->pause_button = ui_button_new(context.view->arena, 0, tr("Pause"), true, false, false);
    gtk_box_append(GTK_BOX(button_group), card->pause_button->widget);

    card->reset_button = ui_button_new(context.view->arena, 0, tr("Reset"), true, false, false);
    gtk_box_append(GTK_BOX(button_group), card->reset_button->widget);

    //
    // message
    //
    if (timer->message.count) {
        UiMarkup *msg = ui_markup_new(mem, timer->message);
        gtk_box_append(GTK_BOX(card->widget->body), msg->widget);
    }

    //
    // listen
    //
    Auto scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
    gtk_widget_add_controller(card->label, scroll);
    g_signal_connect(scroll, "scroll", G_CALLBACK(+[](GtkEventControllerScroll *, F64 dx, F64 dy, Void *data){
        Auto card = reinterpret_cast<TimerCard*>(data);

        if (dy > 0) {
            card->timer->clock_size -= 10;
        } else {
            card->timer->clock_size += 10;
        }

        if (card->timer->clock_size < 10) card->timer->clock_size = 10;
        ui_set_label_font_size(GTK_LABEL(card->label), card->timer->clock_size);
        flush_settings();

        return true;
    }), card);
    g_signal_connect(edit_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto card = reinterpret_cast<TimerCard*>(data);
        editor_view_new(card->timer);
    }), card);
    g_signal_connect(delete_button->confirm, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto card = reinterpret_cast<TimerCard*>(data);
        timer_del(context.core, card->timer);
        flush_settings();
        main_view_new();
    }), card);
    g_signal_connect(card->start_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto card = reinterpret_cast<TimerCard*>(data);
        timer_start(context.core, card->timer);
        if (! context.tic_id) tic();
        timer_card_update(card);
    }), card);
    g_signal_connect(card->pause_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto card = reinterpret_cast<TimerCard*>(data);
        timer_pause(context.core, card->timer);
        timer_card_update(card);
    }), card);
    g_signal_connect(card->reset_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto card = reinterpret_cast<TimerCard*>(data);
        timer_reset(context.core, card->timer);
        timer_card_update(card);
    }), card);
    g_signal_connect(card->widget->widget, "unmap", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto card = reinterpret_cast<TimerCard*>(data);
        if (card->sound) {
            gtk_media_stream_pause(card->sound);
            g_object_unref(card->sound);
        }
        array_find_remove_fast(&context.cards, [&](TimerCard *it){ return it == data; });
        array_maybe_decrease_capacity(&context.cards);
    }), card);

    return card;
}

// =============================================================================
// Editor View
// =============================================================================
struct ViewEditor {
    View base;
    Timer *timer;
    UiMarkupEditor2 *editor;
    UiTimePicker *time_picker;
    UiFilePicker *sound_file;
    UiIntPicker *clock_size;
};

static Void editor_view_new (Timer *timer) {
    tmem_new(tm);

    if (context.view) ui_view_destroy(context.view);
    Auto view = ui_view_new<ViewEditor>(4*KB, context.pub.roots.box, true, true);
    context.view = &view->base;
    view->timer = timer;

    //
    // editor
    //
    view->editor = ui_markup_editor2_new(context.view->arena, timer ? timer->message : str(""), tr("Message"), tr("Message Preview"));
    gtk_box_append(GTK_BOX(view->base.box_left), view->editor->entry_card->widget);
    gtk_box_append(GTK_BOX(view->base.box_right), view->editor->markup_card->widget);

    g_idle_add([](Void *data){
        Auto view = reinterpret_cast<ViewEditor*>(data);
        gtk_widget_grab_focus(view->editor->ed->text_view->entry);
        return 0;
    }, view);

    //
    // time preset picker
    //
    GtkWidget *group = ui_row_group_new();
    gtk_box_append(GTK_BOX(view->base.box_left), group);

    view->time_picker = ui_time_picker_new(context.view->arena, false);
    if (timer) ui_time_picker_set(view->time_picker, timer->preset);
    UiRow *row = ui_row_new(context.view->arena, str(tr("Time")), view->time_picker->widget);
    gtk_box_append(GTK_BOX(group), row->widget);

    //
    // clock size
    //
    view->clock_size = ui_int_picker_new(context.view->arena, 0, 1000, 0);
    if (timer) ui_int_picker_set_value(view->clock_size, timer->clock_size);
    row = ui_row_new(context.view->arena, str(tr("Clock size")), view->clock_size->widget);
    gtk_box_append(GTK_BOX(group), row->widget);

    //
    // sound picker
    //
    view->sound_file = ui_file_picker_new(context.view->arena, tr("Select sound file..."), false, false, str("data/sounds"));
    if (timer) gtk_editable_set_text(GTK_EDITABLE(view->sound_file->entry), cstr(tm, timer->sound));
    row = ui_row_new(context.view->arena, str(tr("Sound")), view->sound_file->widget);
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
        Millisec time  = ui_time_picker_get(view->time_picker);
        String sound   = str(gtk_editable_get_text(GTK_EDITABLE(view->sound_file->entry)));
        I64 clock_size = view->clock_size->val;

        Timer *timer = timer_add(context.core, time, sound, clock_size, msg);
        array_insert(&context.core->timers, array_pop(&context.core->timers), 0);

        if (view->timer) {
            timer->remaining = view->timer->remaining;
            timer->state = view->timer->state;
            timer->start = view->timer->start;
            timer_del(context.core, view->timer);
        }

        flush_settings();
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
    UiScrollbox *searched_cards;
    Array<Timer*> search_results;
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
    array_iter (timer, &context.core->timers) {
        U64 idx = str_search(needle, timer->message);
        if (idx != ARRAY_NIL_IDX) array_push(&view->search_results, timer);
    }

    array_iter (timer, &view->search_results) {
        TimerCard *card = timer_card_new(context.view->arena, timer);
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
    gtk_entry_set_placeholder_text(GTK_ENTRY(view->entry), tr("Search timers..."));
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
    UiRow *row = ui_row_new(context.view->arena, str(tr("Delete selected timers")), view->delete_checkbox);
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
            array_iter (timer, &view->search_results) timer_del(context.core, timer);
            flush_settings();
        }

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
    // timers
    //
    array_iter (timer, &context.core->timers) {
        TimerCard *card = timer_card_new(context.view->arena, timer);
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
    astr_push_cstr(&a, "timers = [\n");
    array_iter (timer, &context.core->timers) {
        astr_push_cstr(&a, "    {\n");
        astr_push_fmt(&a, "         preset     = %lu\n", timer->preset);
        astr_push_fmt(&a, "         sound      = \"%.*s\"\n", STR(timer->sound));
        astr_push_fmt(&a, "         message    = \"%.*s\"\n", STR(timer->message));
        astr_push_fmt(&a, "         clock_size = %lu\n", timer->clock_size);
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
    ConfigAst *val = config_get_array(p, p->root, "timers", &error);
    if (! error) {
        array_iter (timer, &val->children) {
            Bool error = false;
            U64 time = config_get_u64(p, timer, "preset", &error);
            String sound = config_get_string(p, timer, "sound", &error);
            U64 clock_size = config_get_u64(p, timer, "clock_size", &error);
            String msg = config_get_string(p, timer, "message", &error);
            if (! error) timer_add(context.core, time, sound, clock_size, msg);
        }
    }

    astr_println(&log);
}

Applet *ui_timer_init (AppletRoots *roots) {
    context.pub.roots = *roots;
    context.pub.name = str("Timer");
    context.core = timer_new(&mem_root);

    context.settings_path = astr_fmt(&mem_root, "%.*s/timer.txt", STR(main_context.config_dir_path));
    if (! fs_file_exists(context.settings_path)) fs_write_entire_file(context.settings_path, str("timers = []"));

    array_init(&context.cards, &mem_root);
    load_settings();
    main_view_new();
    return &context.pub;
}
