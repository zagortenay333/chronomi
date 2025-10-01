#include "ui/pomodoro.h"
#include "ui/util.h"
#include "ui/widgets.h"
#include "ui/markup.h"
#include "core/config.h"
#include "core/pomodoro.h"
#include "os/fs.h"

struct PomoCard {
    UiCard *widget;
    Pomodoro *pomo;
    GtkWidget *label;
    UiButton *start_button;
    UiButton *pause_button;
    GtkWidget *phase_indicator;
    GtkWidget *pomo_counter;
    GtkWidget *time_to_long_break;
    GtkMediaStream *sound;
};

struct Context {
    Applet pub;
    View *view;
    I64 tic_id;
    PomodoroApplet *core;
    String settings_path;
    Array<PomoCard*> cards;
};

Slice<U64> phase_enums; // PomodoroPhase enums.
Slice<String> phase_labels;

static Context context;

static Void main_view_new ();
static Void flush_settings ();
static Void editor_view_new (Pomodoro *);
static Void pomo_card_update (PomoCard *);
static Void pomo_card_update_label (PomoCard *);

static Void tic () {
    Bool running = pomodoro_tic(context.core);

    if (context.core->phase_changes.count) {
        flush_settings();
        main_view_new();
    } else {
        array_iter (card, &context.cards) {
            if (card->pomo->running)
                pomo_card_update_label(card);
        }
    }

    context.tic_id = running ? g_timeout_add_seconds(1, [](Void*){ tic(); return 0; }, 0) : 0;
}

// =============================================================================
// Pomo Card
// =============================================================================
static Void pomo_card_update_label (PomoCard *card) {
    tmem_new(tm);

    U64 seconds  = (card->pomo->remaining / 1000) % 60;
    U64 minutes  = (card->pomo->remaining / 60000) % 60;
    U64 hours    = card->pomo->remaining / 3600000;
    String label = astr_fmt(tm, "%02lu:%02lu:%02lu%c", hours, minutes, seconds, 0);

    gtk_label_set_text(GTK_LABEL(card->label), label.data);
}

static Void pomo_card_update (PomoCard *card) {
    gtk_widget_remove_css_class(card->widget->header, "kronomi-bg-warning");
    gtk_widget_remove_css_class(card->widget->header, "kronomi-bg-success");
    gtk_widget_set_visible(card->start_button->widget, false);
    gtk_widget_set_visible(card->pause_button->widget, false);

    if (card->pomo->running) {
        gtk_widget_set_visible(card->pause_button->widget, true);

        if (card->pomo->phase == POMO_PHASE_WORK) {
            gtk_widget_add_css_class(card->widget->header, "kronomi-bg-warning");
        } else {
            gtk_widget_add_css_class(card->widget->header, "kronomi-bg-success");
        }
    } else {
        gtk_widget_set_visible(card->start_button->widget, true);
    }

    tmem_new(tm);
    AString l = astr_new(tm);

    astr_push_str(&l, array_get(&phase_labels, card->pomo->phase));
    astr_push_byte(&l, 0);
    gtk_label_set_text(GTK_LABEL(card->phase_indicator), l.data);

    l.count = 0;
    astr_push_fmt(&l, "%lu%c", card->pomo->n_completed_pomos, 0);
    gtk_label_set_text(GTK_LABEL(card->pomo_counter), l.data);

    l.count = 0;
    astr_push_fmt(&l, "%lu%c", card->pomo->pomos_until_long_break, 0);
    gtk_label_set_text(GTK_LABEL(card->time_to_long_break), l.data);

    pomo_card_update_label(card);
}

static Void pomo_card_stop_sound (PomoCard *card) {
    if (card->sound) {
        gtk_media_stream_pause(card->sound);
        g_object_unref(card->sound);
        card->sound = 0;
    }
}

static PomoCard *pomo_card_new (Mem *mem, Pomodoro *pomo) {
    Auto card = mem_new(mem, PomoCard);
    card->widget = ui_card_new(mem);
    card->pomo = pomo;
    array_push(&context.cards, card);
    defer { pomo_card_update(card); };

    //
    // sound
    //
    if (array_has(&context.core->phase_changes, card->pomo) && !card->sound) {
        tmem_new(tm);
        card->sound = gtk_media_file_new_for_filename(cstr(tm, card->pomo->sound_file));
        gtk_media_stream_play(card->sound);
        g_signal_connect(card->sound, "notify::ended", G_CALLBACK(+[](GObject *obj, Void *, Void *data){
            Auto card = reinterpret_cast<PomoCard*>(data);
            pomo_card_stop_sound(card);
        }), card);
    }

    //
    // label
    //
    card->label = gtk_label_new(0);
    gtk_box_append(GTK_BOX(card->widget->left_header), card->label);
    if (card->pomo->clock_size > 10) ui_set_label_font_size(GTK_LABEL(card->label), card->pomo->clock_size);

    //
    // autohide buttons
    //
    UiButton *edit_button = ui_button_new(mem, "kronomi-edit-symbolic", 0, false, false, true);
    gtk_box_append(GTK_BOX(card->widget->autohide_header), edit_button->widget);

    UiPopoverConfirm *delete_button = ui_popover_confirm_new(mem, "kronomi-trash-symbolic", 0, false, false, true);
    gtk_box_append(GTK_BOX(card->widget->autohide_header), delete_button->widget->widget);

    //
    // phase indicator
    //
    GtkWidget *group = ui_row_group_new();
    gtk_box_append(GTK_BOX(card->widget->body), group);

    card->phase_indicator = gtk_label_new(0);
    UiRow *row = ui_row_new(context.view->arena, str(tr("Phase")), card->phase_indicator);
    gtk_box_append(GTK_BOX(group), row->widget);

    //
    // completion counter
    //
    card->pomo_counter = gtk_label_new(0);
    row = ui_row_new(context.view->arena, str(tr("Completed pomos")), card->pomo_counter);
    gtk_box_append(GTK_BOX(group), row->widget);

    //
    // time to long break counter
    //
    card->time_to_long_break = gtk_label_new(0);
    row = ui_row_new(context.view->arena, str(tr("Pomos until long break")), card->time_to_long_break);
    gtk_box_append(GTK_BOX(group), row->widget);

    //
    // timer buttons
    //
    GtkWidget *button_group = ui_button_group_new(false, true);
    gtk_box_append(GTK_BOX(card->widget->body), button_group);

    card->start_button = ui_button_new(context.view->arena, 0, tr("Start"), true, false, false);
    gtk_box_append(GTK_BOX(button_group), card->start_button->widget);

    card->pause_button = ui_button_new(context.view->arena, 0, tr("Pause"), true, false, false);
    gtk_box_append(GTK_BOX(button_group), card->pause_button->widget);

    //
    // message
    //
    if (pomo->message.count) {
        UiMarkup *msg = ui_markup_new(mem, pomo->message);
        gtk_box_append(GTK_BOX(card->widget->body), msg->widget);
    }

    //
    // listen
    //
    Auto scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
    gtk_widget_add_controller(card->label, scroll);
    g_signal_connect(scroll, "scroll", G_CALLBACK(+[](GtkEventControllerScroll *, F64 dx, F64 dy, Void *data){
        Auto card = reinterpret_cast<PomoCard*>(data);

        if (dy > 0) {
            card->pomo->clock_size -= 10;
        } else {
            card->pomo->clock_size += 10;
        }

        if (card->pomo->clock_size < 10) card->pomo->clock_size = 10;
        ui_set_label_font_size(GTK_LABEL(card->label), card->pomo->clock_size);
        flush_settings();

        return true;
    }), card);
    g_signal_connect(edit_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto card = reinterpret_cast<PomoCard*>(data);
        editor_view_new(card->pomo);
    }), card);
    g_signal_connect(delete_button->confirm, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto card = reinterpret_cast<PomoCard*>(data);
        pomodoro_del(context.core, card->pomo);
        flush_settings();
        main_view_new();
    }), card);
    g_signal_connect(card->start_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto card = reinterpret_cast<PomoCard*>(data);
        pomodoro_start(context.core, card->pomo);
        if (! context.tic_id) tic();
        pomo_card_update(card);
    }), card);
    g_signal_connect(card->pause_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto card = reinterpret_cast<PomoCard*>(data);
        pomodoro_pause(context.core, card->pomo);
        if (card->sound) pomo_card_stop_sound(card);
        pomo_card_update(card);
    }), card);
    g_signal_connect(card->widget->widget, "unmap", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto card = reinterpret_cast<PomoCard*>(data);
        pomo_card_stop_sound(card);
        array_find_remove_fast(&context.cards, [&](PomoCard *it){ return it == data; });
        array_maybe_decrease_capacity(&context.cards);
    }), card);

    return card;
}

// =============================================================================
// Editor View
// =============================================================================
struct ViewEditor {
    View base;
    Pomodoro *pomo;
    UiMarkupEditor2 *editor;
    UiTimePicker *work_time;
    UiTimePicker *sbreak_time;
    UiTimePicker *lbreak_time;
    UiDropdown *phase_selector;
    UiIntPicker *completed_pomos;
    UiIntPicker *long_break_cycle;
    UiIntPicker *time_to_long_break;
    UiIntPicker *clock_size;
    UiFilePicker *sound_file;
};

static Void editor_view_new (Pomodoro *pomo) {
    tmem_new(tm);

    if (context.view) ui_view_destroy(context.view);
    Auto view = ui_view_new<ViewEditor>(4*KB, context.pub.roots.box, true, true);
    context.view = &view->base;
    view->pomo = pomo;

    //
    // editor
    //
    view->editor = ui_markup_editor2_new(context.view->arena, pomo ? pomo->message : str(""), tr("Message"), tr("Message Preview"));
    gtk_box_append(GTK_BOX(view->base.box_left), view->editor->entry_card->widget);
    gtk_box_append(GTK_BOX(view->base.box_right), view->editor->markup_card->widget);

    g_idle_add([](Void *data){
        Auto view = reinterpret_cast<ViewEditor*>(data);
        gtk_widget_grab_focus(view->editor->ed->text_view->entry);
        return 0;
    }, view);

    //
    // work time picker
    //
    GtkWidget *group = ui_row_group_new();
    gtk_box_append(GTK_BOX(view->base.box_left), group);

    view->work_time = ui_time_picker_new(context.view->arena, false);
    if (pomo) ui_time_picker_set(view->work_time, pomo->work_length);
    UiRow *row = ui_row_new(context.view->arena, str(tr("Work duration")), view->work_time->widget);
    gtk_box_append(GTK_BOX(group), row->widget);

    //
    // short break time picker
    //
    view->sbreak_time = ui_time_picker_new(context.view->arena, false);
    if (pomo) ui_time_picker_set(view->sbreak_time, pomo->short_break_length);
    row = ui_row_new(context.view->arena, str(tr("Short break duration")), view->sbreak_time->widget);
    gtk_box_append(GTK_BOX(group), row->widget);

    //
    // long break time picker
    //
    view->lbreak_time = ui_time_picker_new(context.view->arena, false);
    if (pomo) ui_time_picker_set(view->lbreak_time, pomo->long_break_length);
    row = ui_row_new(context.view->arena, str(tr("Long break duration")), view->lbreak_time->widget);
    gtk_box_append(GTK_BOX(group), row->widget);

    //
    // phase selector
    //
    view->phase_selector = ui_dropdown_new(context.view->arena, (pomo ? pomo->phase : POMO_PHASE_WORK), phase_enums, phase_labels);
    row = ui_row_new(context.view->arena, str(tr("Phase")), view->phase_selector->widget->widget);
    gtk_box_append(GTK_BOX(group), row->widget);

    //
    // completed pomo counter
    //
    view->completed_pomos = ui_int_picker_new(context.view->arena, 0, INT64_MAX, (pomo ? pomo->n_completed_pomos : 0));
    row = ui_row_new(context.view->arena, str(tr("Completed pomodoros")), view->completed_pomos->widget);
    gtk_box_append(GTK_BOX(group), row->widget);

    //
    // long break every n pomos
    //
    view->long_break_cycle = ui_int_picker_new(context.view->arena, 0, INT64_MAX, (pomo ? pomo->long_break_every_n_pomos : 0));
    row = ui_row_new(context.view->arena, str(tr("Long break every n pomos")), view->long_break_cycle->widget);
    gtk_box_append(GTK_BOX(group), row->widget);

    //
    // pomos until long break selector
    //
    view->time_to_long_break = ui_int_picker_new(context.view->arena, 0, INT64_MAX, (pomo ? pomo->pomos_until_long_break : 0));
    row = ui_row_new(context.view->arena, str(tr("Pomodoros until long break")), view->time_to_long_break->widget);
    gtk_box_append(GTK_BOX(group), row->widget);

    //
    // clock size
    //
    view->clock_size = ui_int_picker_new(context.view->arena, 0, 1000, pomo ? pomo->clock_size : 0);
    row = ui_row_new(context.view->arena, str(tr("Clock size")), view->clock_size->widget);
    gtk_box_append(GTK_BOX(group), row->widget);

    //
    // sound path
    //
    view->sound_file = ui_file_picker_new(context.view->arena, tr("Select sound file..."), false, false, str("data/sounds"));
    if (pomo) gtk_editable_set_text(GTK_EDITABLE(view->sound_file->entry), cstr(tm, pomo->sound_file));
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

        Auto view              = reinterpret_cast<ViewEditor*>(context.view);
        String msg             = ui_text_view_get_text(tm, view->editor->ed->text_view);
        Millisec work_time     = ui_time_picker_get(view->work_time);
        Millisec sbreak_time   = ui_time_picker_get(view->sbreak_time);
        Millisec lbreak_time   = ui_time_picker_get(view->lbreak_time);
        PomodoroPhase phase    = static_cast<PomodoroPhase>(view->phase_selector->selection);
        U64 completed_pomos    = view->completed_pomos->val;
        U64 long_break_cycle   = view->long_break_cycle->val;
        U64 time_to_long_break = view->time_to_long_break->val;
        String sound           = str(gtk_editable_get_text(GTK_EDITABLE(view->sound_file->entry)));
        U64 clock_size         = view->clock_size->val;

        Pomodoro *new_pomo = pomodoro_add(context.core, phase, sound, work_time, sbreak_time, lbreak_time, clock_size, msg, completed_pomos, time_to_long_break, long_break_cycle);

        if (view->pomo) {
            if (phase == view->pomo->phase) {
                new_pomo->start = view->pomo->start;
                new_pomo->running = view->pomo->running;
                new_pomo->remaining = view->pomo->remaining;
            }

            pomodoro_del(context.core, view->pomo);
        }

        array_insert(&context.core->pomodoros, array_pop(&context.core->pomodoros), 0);
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
    Array<Pomodoro*> search_results;
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
    array_iter (pomo, &context.core->pomodoros) {
        U64 idx = str_search(needle, pomo->message);
        if (idx != ARRAY_NIL_IDX) array_push(&view->search_results, pomo);
    }

    array_iter (pomo, &view->search_results) {
        PomoCard *card = pomo_card_new(context.view->arena, pomo);
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
    gtk_entry_set_placeholder_text(GTK_ENTRY(view->entry), tr("Search pomodoros..."));
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
    UiRow *row = ui_row_new(context.view->arena, str(tr("Delete selected pomodoros")), view->delete_checkbox);
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
            array_iter (pomo, &view->search_results) pomodoro_del(context.core, pomo);
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
    // stopwatches
    //
    array_iter (pomo, &context.core->pomodoros) {
        PomoCard *card = pomo_card_new(context.view->arena, pomo);
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
    astr_push_cstr(&a, "pomodoros = [\n");
    array_iter (pomo, &context.core->pomodoros) {
        astr_push_cstr(&a, "    {\n");
        astr_push_fmt(&a,  "        clock_size               = %lu\n", pomo->clock_size);
        astr_push_fmt(&a,  "        message                  = \"%.*s\"\n", STR(pomo->message));
        astr_push_fmt(&a,  "        sound_file               = \"%.*s\"\n", STR(pomo->sound_file));
        astr_push_fmt(&a,  "        work_length              = %lu\n", pomo->work_length);
        astr_push_fmt(&a,  "        short_break_length       = %lu\n", pomo->short_break_length);
        astr_push_fmt(&a,  "        long_break_length        = %lu\n", pomo->long_break_length);
        astr_push_fmt(&a,  "        long_break_every_n_pomos = %lu\n", pomo->long_break_every_n_pomos);
        astr_push_fmt(&a,  "        n_completed_pomos        = %lu\n", pomo->n_completed_pomos);
        astr_push_fmt(&a,  "        pomos_until_long_break   = %lu\n", pomo->pomos_until_long_break);
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
    ConfigAst *val = config_get_array(p, p->root, "pomodoros", &error);
    if (! error) {
        array_iter (sw, &val->children) {
            Bool error = false;
            U64 clock_size = config_get_u64(p, sw, "clock_size", &error);
            String msg = config_get_string(p, sw, "message", &error);
            String sound = config_get_string(p, sw, "sound_file", &error);
            U64 work_length = config_get_u64(p, sw, "work_length", &error);
            U64 short_break_length = config_get_u64(p, sw, "short_break_length", &error);
            U64 long_break_length = config_get_u64(p, sw, "long_break_length", &error);
            U64 long_break_every_n_pomos = config_get_u64(p, sw, "long_break_every_n_pomos", &error);
            U64 n_completed_pomos = config_get_u64(p, sw, "n_completed_pomos", &error);
            U64 pomos_until_long_break = config_get_u64(p, sw, "pomos_until_long_break", &error);
            if (! error) pomodoro_add(context.core, POMO_PHASE_WORK, sound, work_length, short_break_length, long_break_length, clock_size, msg, n_completed_pomos, pomos_until_long_break, long_break_every_n_pomos);
        }
    }

    astr_println(&log);
}

Applet *ui_pomodoro_init (AppletRoots *roots) {
    context.pub.roots = *roots;
    context.pub.name = str("Pomodoro");
    context.core = pomodoro_new(&mem_root);
    array_init(&context.cards, &mem_root);

    context.settings_path = astr_fmt(&mem_root, "%.*s/pomodoro.txt", STR(main_context.config_dir_path));
    if (! fs_file_exists(context.settings_path)) fs_write_entire_file(context.settings_path, str("pomodoros = []"));

    Auto enums = array_new_cap<U64>(&mem_root, 3);
    array_push_n(&enums, POMO_PHASE_WORK, POMO_PHASE_SHORT_BREAK, POMO_PHASE_LONG_BREAK);
    phase_enums = slice(&enums);

    Auto labels = array_new_cap<String>(&mem_root, 3);
    array_push_n(&labels, str(tr("Work")), str(tr("Short Break")), str(tr("Long Break")));
    phase_labels = slice(&labels);

    load_settings();
    main_view_new();
    return &context.pub;
}
