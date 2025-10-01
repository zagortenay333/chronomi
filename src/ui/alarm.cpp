#include "ui/alarm.h"
#include "ui/widgets.h"
#include "ui/markup.h"
#include "ui/util.h"
#include "util/core.h"
#include "core/config.h"
#include "core/alarm.h"
#include "os/fs.h"

struct AlarmCard {
    UiCard *widget;
    Alarm *alarm;
    GtkWidget *label;
    GtkWidget *toggle;
    GtkWidget *button_group;
    GtkMediaStream *sound;
    Bool block_switch_handler;
};

struct Context {
    Applet pub;
    AlarmApplet *core;
    View *view;
    I64 tic_id;
    String settings_path;
};

static Context context;

static Void main_view_new ();
static Void flush_settings ();
static Void editor_view_new (Alarm *);

static Void tic () {
    Bool playing = alarm_tic(context.core);
    if (playing) main_view_new();
    context.tic_id = g_timeout_add_seconds(1, [](Void*){ tic(); return 0; }, 0);
}

// =============================================================================
// Alarm Card
// =============================================================================
static Void alarm_card_update (AlarmCard *card) {
    card->block_switch_handler = true;
    gtk_switch_set_active(GTK_SWITCH(card->toggle), card->alarm->enabled);
    gtk_switch_set_state(GTK_SWITCH(card->toggle), card->alarm->enabled);
    card->block_switch_handler = false;

    if (card->alarm->playing) {
        gtk_widget_add_css_class(card->widget->body, "kronomi-bg-error");
        gtk_widget_set_visible(card->widget->header, false);
        gtk_widget_set_visible(card->button_group, true);

        if (! card->sound) {
            tmem_new(tm);
            card->sound = gtk_media_file_new_for_filename(cstr(tm, card->alarm->sound));
            gtk_media_stream_set_loop(card->sound, true);
            gtk_media_stream_play(card->sound);
        }
    } else {
        gtk_widget_remove_css_class(card->widget->body, "kronomi-bg-error");
        gtk_widget_set_visible(card->widget->header, true);
        gtk_widget_set_visible(card->button_group, false);

        if (card->sound) {
            gtk_media_stream_pause(card->sound);
            g_object_unref(card->sound);
            card->sound = 0;
        }
    }

    tmem_new(tm);
    String label = astr_fmt(tm, "%02lu:%02lu%c", card->alarm->time / 60, card->alarm->time % 60, 0);
    gtk_label_set_text(GTK_LABEL(card->label), label.data);
}

static AlarmCard *alarm_card_new (Mem *mem, Alarm *alarm) {
    Auto card = mem_new(mem, AlarmCard);
    card->widget = ui_card_new(mem);
    card->alarm = alarm;
    defer { alarm_card_update(card); };

    //
    // label
    //
    card->label = gtk_label_new(0);
    gtk_box_append(GTK_BOX(card->widget->left_header), card->label);

    //
    // autohide buttons
    //
    UiButton *edit_button = ui_button_new(mem, "kronomi-edit-symbolic", 0, false, false, true);
    gtk_box_append(GTK_BOX(card->widget->autohide_header), edit_button->widget);

    UiPopoverConfirm *delete_button = ui_popover_confirm_new(mem, "kronomi-trash-symbolic", 0, false, false, true);
    gtk_box_append(GTK_BOX(card->widget->autohide_header), delete_button->widget->widget);

    //
    // switch
    //
    card->toggle = ui_switch_new(card->alarm->enabled);
    gtk_box_append(GTK_BOX(card->widget->header), card->toggle);

    //
    // buttons
    //
    card->button_group = ui_button_group_new(false, true);
    gtk_box_append(GTK_BOX(card->widget->body), card->button_group);

    UiButton *dismiss_button = ui_button_new(context.view->arena, 0, tr("Dismiss"), true, false, false);
    gtk_box_append(GTK_BOX(card->button_group), dismiss_button->widget);

    UiButton *snooze_button = ui_button_new(context.view->arena, 0, tr("Snooze"), true, false, false);
    gtk_box_append(GTK_BOX(card->button_group), snooze_button->widget);

    //
    // message
    //
    if (alarm->message.count) {
        UiMarkup *msg = ui_markup_new(mem, alarm->message);
        gtk_box_append(GTK_BOX(card->widget->body), msg->widget);
    }

    //
    // listen
    //
    g_signal_connect(edit_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto card = reinterpret_cast<AlarmCard*>(data);
        editor_view_new(card->alarm);
    }), card);
    g_signal_connect(delete_button->confirm, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto card = reinterpret_cast<AlarmCard*>(data);
        alarm_del(context.core, card->alarm);
        flush_settings();
        main_view_new();
    }), card);
    g_signal_connect(card->toggle, "state-set", G_CALLBACK(+[](GtkSwitch *s, Bool state, Void *data){
        Auto card = reinterpret_cast<AlarmCard*>(data);
        if (card->block_switch_handler) return true;
        alarm_toggle(context.core, card->alarm);
        flush_settings();
        alarm_card_update(card);
        return false;
    }), card);
    g_signal_connect(dismiss_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto card = reinterpret_cast<AlarmCard*>(data);
        card->alarm->playing = false;
        main_view_new();
    }), card);
    g_signal_connect(snooze_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto card = reinterpret_cast<AlarmCard*>(data);
        alarm_snooze(context.core, card->alarm);
        main_view_new();
    }), card);
    g_signal_connect(card->widget->widget, "unmap", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto card = reinterpret_cast<AlarmCard*>(data);
        if (card->sound) {
            gtk_media_stream_pause(card->sound);
            g_object_unref(card->sound);
        }
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
    Array<Alarm*> search_results;
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
    array_iter (alarm, &context.core->alarms) {
        U64 idx = str_search(needle, alarm->message);
        if (idx != ARRAY_NIL_IDX) array_push(&view->search_results, alarm);
    }

    array_iter (alarm, &view->search_results) {
        AlarmCard *card = alarm_card_new(context.view->arena, alarm);
        gtk_box_append(GTK_BOX(view->searched_cards->box), card->widget->widget);
    }
}

static Void search_view_new () {
    if (context.view) ui_view_destroy(context.view);
    Auto view = ui_view_new<ViewSearch>(4*KB, context.pub.roots.box, true, false);
    context.view = &view->base;
    array_init(&view->search_results, context.view->arena);
    defer { search_view_do_search(); };

    //
    // search entry
    //
    view->entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(view->entry), tr("Search alarms..."));
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
    UiRow *row = ui_row_new(context.view->arena, str(tr("Delete selected alarms")), view->delete_checkbox);
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
            array_iter (alarm, &view->search_results) alarm_del(context.core, alarm);
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
    Alarm *alarm;
    UiMarkupEditor2 *editor;
    UiDayPicker *days;
    UiIntPicker *snooze;
    UiTimePicker *time;
    UiFilePicker *sound_file;
};

static Void editor_view_new (Alarm *alarm) {
    tmem_new(tm);

    if (context.view) ui_view_destroy(context.view);
    Auto view = ui_view_new<ViewEditor>(4*KB, context.pub.roots.box, true, true);
    context.view = &view->base;
    view->alarm = alarm;

    //
    // editor
    //
    view->editor = ui_markup_editor2_new(context.view->arena, alarm ? alarm->message : str(""), tr("Message"), tr("Message Preview"));
    gtk_box_append(GTK_BOX(view->base.box_left), view->editor->entry_card->widget);
    gtk_box_append(GTK_BOX(view->base.box_right), view->editor->markup_card->widget);

    g_idle_add([](Void *data){
        Auto view = reinterpret_cast<ViewEditor*>(data);
        gtk_widget_grab_focus(view->editor->ed->text_view->entry);
        return 0;
    }, view);

    //
    // day picker
    //
    view->days = ui_day_picker_new(context.view->arena, (alarm ? alarm->days : 0xff));
    gtk_box_append(GTK_BOX(view->base.box_left), view->days->widget);

    //
    // time selector
    //
    GtkWidget *group = ui_row_group_new();
    gtk_box_append(GTK_BOX(view->base.box_left), group);

    view->time = ui_time_picker_new(context.view->arena, true);
    if (alarm) ui_time_picker_set(view->time, alarm->time * 60000);
    UiRow *row = ui_row_new(context.view->arena, str(tr("Time")), view->time->widget);
    gtk_box_append(GTK_BOX(group), row->widget);

    //
    // snooze time
    //
    view->snooze = ui_int_picker_new(context.view->arena, 1, 24*60, (alarm ? alarm->snooze : 1));
    row = ui_row_new(context.view->arena, str(tr("Snooze time in minutes")), view->snooze->widget);
    gtk_box_append(GTK_BOX(group), row->widget);

    //
    // sound file
    //
    view->sound_file = ui_file_picker_new(context.view->arena, tr("Select sound file..."), false, false, str("data/sounds"));
    if (alarm) gtk_editable_set_text(GTK_EDITABLE(view->sound_file->entry), cstr(tm, alarm->sound));
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

        Auto view = reinterpret_cast<ViewEditor*>(context.view);

        Alarm a;
        a.time    = ui_time_picker_get(view->time) / 60000;
        a.snooze  = view->snooze->val;
        a.message = ui_text_view_get_text(tm, view->editor->ed->text_view);
        a.sound   = str(gtk_editable_get_text(GTK_EDITABLE(view->sound_file->entry)));
        a.enabled = view->alarm ? view->alarm->enabled : true;
        a.playing = view->alarm ? view->alarm->playing : false;
        a.days    = ui_day_picker_get_value(view->days);

        if (view->alarm) alarm_del(context.core, view->alarm);
        alarm_add(context.core, &a);

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
    // cards
    //
    array_iter (alarm, &context.core->alarms) {
        AlarmCard *card = alarm_card_new(context.view->arena, alarm);
        if (alarm->playing) {
            gtk_box_prepend(GTK_BOX(view->base.box_right), card->widget->widget);
        } else {
            gtk_box_append(GTK_BOX(view->base.box_right), card->widget->widget);
        }
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
    astr_push_cstr(&a, "alarms = [\n");
    array_iter (alarm, &context.core->alarms) {
        astr_push_cstr(&a, "    {\n");
        astr_push_fmt(&a, "         time    = %lu\n", alarm->time);
        astr_push_fmt(&a, "         snooze  = %lu\n", alarm->snooze);
        astr_push_fmt(&a, "         sound   = \"%.*s\"\n", STR(alarm->sound));
        astr_push_fmt(&a, "         message = \"%.*s\"\n", STR(alarm->message));
        astr_push_fmt(&a, "         enabled = %s\n", alarm->enabled ? "true" : "false");
        astr_push_fmt(&a, "         days    = %u\n", alarm->days);
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
    ConfigAst *val = config_get_array(p, p->root, "alarms", &error);
    if (! error) {
        array_iter (alarm, &val->children) {
            Bool error = false;

            Alarm a   = {};
            a.time    = config_get_u64(p, alarm, "time", &error);
            a.snooze  = config_get_u64(p, alarm, "snooze", &error);
            a.message = config_get_string(p, alarm, "message", &error);
            a.sound   = config_get_string(p, alarm, "sound", &error);
            a.enabled = config_get_bool(p, alarm, "enabled", &error);
            a.playing = false;
            a.days    = static_cast<U8>(config_get_u64(p, alarm, "days", &error));

            if (! error) alarm_add(context.core, &a);
        }
    }

    astr_println(&log);
}

Applet *ui_alarm_init (AppletRoots *roots) {
    context.pub.roots = *roots;
    context.pub.name = str("Alarm");
    context.core = alarm_new(&mem_root);

    context.settings_path = astr_fmt(&mem_root, "%.*s/alarm.txt", STR(main_context.config_dir_path));
    if (! fs_file_exists(context.settings_path)) fs_write_entire_file(context.settings_path, str("alarms = []"));

    load_settings();
    tic();
    main_view_new();
    return &context.pub;
}
