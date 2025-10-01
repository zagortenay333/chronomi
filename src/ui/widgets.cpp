#include "ui/main.h"
#include "ui/widgets.h"
#include "ui/markup.h"
#include "ui/util.h"

// =============================================================================
// Switch
// =============================================================================
GtkWidget *ui_switch_new (Bool active) {
    Auto s = gtk_switch_new();
    gtk_widget_set_hexpand(s, false);
    gtk_widget_set_halign(s, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(s, false);
    gtk_widget_set_valign(s, GTK_ALIGN_CENTER);
    return s;
}

// =============================================================================
// Box
// =============================================================================
GtkWidget *ui_box_new (Bool vertical) {
    GtkWidget *box = gtk_box_new (vertical ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL, SPACING);
    gtk_widget_add_css_class(box, "frame");
    gtk_widget_add_css_class(box, "kronomi-box");
    return box;
}

// =============================================================================
// Buttons
// =============================================================================
GtkWidget *ui_button_group_new (Bool vertical, Bool homogenous) {
    Auto g = gtk_box_new(vertical ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(g, "linked");
    gtk_box_set_homogeneous(GTK_BOX(g), homogenous);
    return g;
}

UiButton *ui_button_new (Mem *mem, CString icon_name, CString label, Bool wide, Bool icon_after_label, Bool floating) {
    Auto button = mem_new(mem, UiButton);

    button->widget = gtk_button_new();
    gtk_widget_add_css_class(button->widget, "kronomi-button");
    if (floating) gtk_button_set_has_frame(GTK_BUTTON(button->widget), false);
    gtk_widget_set_hexpand(button->widget, false);
    gtk_widget_set_halign(button->widget, GTK_ALIGN_CENTER);

    Auto box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, SPACING);
    gtk_button_set_child(GTK_BUTTON(button->widget), box);

    if (label) {
        button->label = gtk_label_new(label);
        gtk_box_append(GTK_BOX(box), button->label);

        if (wide) {
            gtk_widget_set_hexpand(button->label, true);
            gtk_widget_set_halign(button->label, GTK_ALIGN_FILL);
            gtk_widget_set_hexpand(button->widget, true);
            gtk_widget_set_halign(button->widget, GTK_ALIGN_FILL);
        }
    }

    if (icon_name) {
        button->icon = gtk_image_new_from_icon_name(icon_name);
        if (icon_after_label) gtk_box_append(GTK_BOX(box), button->icon);
        else                  gtk_box_prepend(GTK_BOX(box), button->icon);
    }

    return button;
}

// =============================================================================
// Checkbox
// =============================================================================
Bool ui_checkbox_checked (GtkWidget *w) {
    return gtk_check_button_get_active(GTK_CHECK_BUTTON(w));
}

// =============================================================================
// File Picker
// =============================================================================
UiFilePicker *ui_file_picker_new (Mem *mem, CString hint, Bool multiple, Bool dirs, String start_dir) {
    Auto picker = mem_new(mem, UiFilePicker);
    picker->multiple = multiple;
    picker->dirs = dirs;
    picker->start_dir = start_dir;

    picker->widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(picker->widget, "kronomi-file-picker");

    picker->entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(picker->entry), hint ? hint : tr("Select file..."));
    gtk_box_append(GTK_BOX(picker->widget), picker->entry);

    UiButton *button = ui_button_new(mem, "kronomi-search-symbolic", 0, false, false, false);
    gtk_box_append(GTK_BOX(picker->widget), button->widget);

    //
    // listen
    //
    g_signal_connect(button->widget, "clicked", G_CALLBACK(+[](GtkWidget *widget, gpointer data){
        Auto picker = static_cast<UiFilePicker*>(data);
        GtkFileDialog *dialog = gtk_file_dialog_new();
        if (picker->start_dir.data) {
            tmem_new(tm);
            GFile *initial = g_file_new_for_path(cstr(tm, picker->start_dir));
            gtk_file_dialog_set_initial_folder(dialog, initial);
            g_object_unref(initial);
        }

        Auto start = picker->dirs ? gtk_file_dialog_select_multiple_folders : gtk_file_dialog_open_multiple;

        start(dialog, GTK_WINDOW(main_context.window), 0, [](GObject *source_object, GAsyncResult *res, gpointer data){
            Auto picker = static_cast<UiFilePicker*>(data);
            GtkFileDialog *dialog = GTK_FILE_DIALOG(source_object);

            Auto finish = picker->dirs ? gtk_file_dialog_select_multiple_folders_finish : gtk_file_dialog_open_multiple_finish;
            GListModel *files = finish(dialog, res, NULL);
            if (! files) return;

            tmem_new(tm);
            AString text = astr_new(tm);

            U64 n = picker->multiple ? g_list_model_get_n_items(files) : 1;
            for (U64 i = 0; i < n; ++i) {
                GFile *file = static_cast<GFile*>(g_list_model_get_item(files, i));
                char *path = g_file_get_path(file);
                astr_push_cstr(&text, path);
                if (i < n-1) astr_push_byte(&text, '|');
                g_free(path);
                g_object_unref(file);
            }

            astr_push_byte(&text, 0);
            gtk_editable_set_text(GTK_EDITABLE(picker->entry), text.data);
            gtk_editable_set_position(GTK_EDITABLE(picker->entry), -1);

            g_object_unref(files);
        }, picker);
    }), picker);

    return picker;
}

// =============================================================================
// Confirm Dialog
// =============================================================================
Void ui_confirm_dialog_new (CString msg, Void (*fn)(Void*), Void *fn_data) {
    Auto c = mem_new(&mem_root, UiConfirmDialog);
    c->fn = fn;
    c->fn_data = fn_data;

    GtkAlertDialog *dialog = gtk_alert_dialog_new(msg);

    CString buttons [] = { tr("Cancel"), tr("Ok"), NULL };
    gtk_alert_dialog_set_buttons (dialog, buttons);

    gtk_alert_dialog_set_cancel_button (dialog, 0);

    Auto on_click = +[](GObject *dialog, GAsyncResult *result, Void *data){
        Int response = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(dialog), result, 0);
        Auto c = reinterpret_cast<UiConfirmDialog*>(data);
        Auto fn = c->fn;
        Auto fn_data = c->fn_data;
        mem_free(&mem_root, .old_ptr=c);
        if (response == 1) fn(fn_data);
    };

    gtk_alert_dialog_choose(dialog, GTK_WINDOW(main_context.window), 0, on_click, c);
}

// =============================================================================
// Popover
// =============================================================================
UiPopover *ui_popover_new (Mem *mem, CString icon_name, CString label, Bool wide, Bool icon_after_label, Bool floating) {
    Auto p = mem_new(mem, UiPopover);

    p->widget = gtk_menu_button_new();
    if (floating) gtk_menu_button_set_has_frame(GTK_MENU_BUTTON(p->widget), false);
    gtk_widget_set_hexpand(p->widget, false);
    gtk_widget_set_halign(p->widget, GTK_ALIGN_CENTER);

    p->popover = gtk_popover_new();
    gtk_widget_add_css_class(p->popover, "kronomi-popover");
    gtk_popover_set_has_arrow(GTK_POPOVER(p->popover), true);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(p->widget), p->popover);

    Auto scrollbox = ui_scrollbox_new(mem, true);
    gtk_popover_set_child(GTK_POPOVER(p->popover), scrollbox->widget);
    p->box = scrollbox->box;

    Auto button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, SPACING);
    gtk_menu_button_set_child(GTK_MENU_BUTTON(p->widget), button_box);

    if (label) {
        p->label = gtk_label_new(label);
        gtk_box_append(GTK_BOX(button_box), p->label);

        if (wide) {
            gtk_widget_set_hexpand(p->label, true);
            gtk_widget_set_halign(p->label, GTK_ALIGN_FILL);
            gtk_widget_set_hexpand(p->widget, true);
            gtk_widget_set_halign(p->widget, GTK_ALIGN_FILL);
        }
    }

    if (icon_name) {
        p->icon = gtk_image_new_from_icon_name(icon_name);
        if (icon_after_label) gtk_box_append(GTK_BOX(button_box), p->icon);
        else                  gtk_box_prepend(GTK_BOX(button_box), p->icon);
    }

    return p;
}

Void ui_popover_set_info (UiPopover *p, Mem *mem, String text) {
    UiMarkup *markup = ui_markup_new(mem, text);
    gtk_box_append(GTK_BOX(p->box), markup->widget);
}

UiPopoverConfirm *ui_popover_confirm_new (Mem *mem, CString icon_name, CString label, Bool wide, Bool icon_after_label, Bool floating) {
    Auto p = mem_new(mem, UiPopoverConfirm);
    p->widget = ui_popover_new(mem, icon_name, label, wide, icon_after_label, floating);

    GtkWidget *msg = gtk_label_new(tr("Are you sure?"));
    gtk_box_append(GTK_BOX(p->widget->box), msg);

    UiButton *button = ui_button_new(mem, 0, tr("Confirm"), true, false, false);
    p->confirm = button->widget;
    gtk_box_append(GTK_BOX(p->widget->box), button->widget);

    return p;
}

// =============================================================================
// Int Picker
// =============================================================================
Void ui_int_picker_set_value (UiIntPicker *p, I64 val) {
    tmem_new(tm);
    String text = astr_fmt(tm, "%li%c", val, 0);
    gtk_editable_set_text(GTK_EDITABLE(p->entry), text.data);
}

Void ui_int_picker_on_value_changed (UiIntPicker *p) {
    String text = str(gtk_editable_get_text(GTK_EDITABLE(p->entry)));

    Bool valid = true;
    array_iter (c, &text) {
        if (c == '-' && ARRAY_IDX == 0) continue;
        if (c < '0' || c > '9') { valid = false; break; }
    }

    I64 val;
    if (valid) valid = str_to_i64(text.data, &val, 10);

    if (valid && (val < p->min || val > p->max)) valid = false;

    if (valid) {
        gtk_widget_remove_css_class(p->widget, "invalid");
        gtk_widget_set_visible(p->info->widget, false);
    } else {
        gtk_widget_add_css_class(p->widget, "invalid");
        gtk_widget_set_visible(p->info->widget, true);
    }

    p->val = valid ? val : p->init;
}

UiIntPicker *ui_int_picker_new (Mem *mem, I64 min, I64 max, I64 initial) {
    Auto p  = mem_new(mem, UiIntPicker);
    p->min  = min;
    p->max  = max;
    p->init = initial;

    p->widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(p->widget, "kronomi-int-picker");

    p->entry = gtk_entry_new();
    gtk_box_append(GTK_BOX(p->widget), p->entry);
    gtk_editable_set_max_width_chars(GTK_EDITABLE(p->entry), 6);
    ui_int_picker_set_value(p, initial);

    p->info = ui_popover_new(mem, "kronomi-issue-symbolic", 0, false, false, false);
    gtk_box_append(GTK_BOX(p->widget), p->info->widget);

    tmem_new(tm);
    String text = astr_fmt(tm, "Input must be an integer between %li and %li.%c", min, max, 0);
    Auto label = gtk_label_new(text.data);
    gtk_box_append(GTK_BOX(p->info->box), label);

    //
    // listen
    //
    Auto scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
    gtk_widget_add_controller(p->entry, scroll);
    g_signal_connect(scroll, "scroll", G_CALLBACK(+[](GtkEventControllerScroll *controller, F64 dx, F64 dy, Void *data){
        Auto p = static_cast<UiIntPicker*>(data);
        if (dy > 0) {
            if (p->val > p->min) p->val--;
        } else {
            if (p->val < p->max) p->val++;
        }
        ui_int_picker_set_value(p, p->val);
        return true;
    }), p);
    g_signal_connect(p->entry, "changed", G_CALLBACK(+[](GtkWidget *widget, gpointer data){
        ui_int_picker_on_value_changed(static_cast<UiIntPicker*>(data));
    }), p);

    ui_int_picker_on_value_changed(p);
    return p;
}

// =============================================================================
// Time Picker
// =============================================================================
Millisec ui_time_picker_get (UiTimePicker *p) {
    Millisec result = 0;
    result += p->hours->val * 3600000;
    result += p->minutes->val * 60000;
    result += p->seconds->val * 1000;
    return result;
}

Void ui_time_picker_set (UiTimePicker *p, Millisec time) {
    ui_int_picker_set_value(p->hours, time / 3600000);
    ui_int_picker_set_value(p->minutes, (time / 60000) % 60);
    ui_int_picker_set_value(p->seconds, (time / 1000) % 60);
}

UiTimePicker *ui_time_picker_new (Mem *mem, Bool clock) {
    Auto p = mem_new(mem, UiTimePicker);

    p->widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 1);
    gtk_widget_add_css_class(p->widget, "kronomi-time-picker");

    p->hours = ui_int_picker_new(mem, 0, clock ? 23 : 1000, 0);
    gtk_box_append(GTK_BOX(p->widget), p->hours->widget);
    gtk_editable_set_max_width_chars(GTK_EDITABLE(p->hours->entry), 2);

    gtk_box_append(GTK_BOX(p->widget), gtk_label_new(":"));

    p->minutes = ui_int_picker_new(mem, 0, 59, 0);
    gtk_box_append(GTK_BOX(p->widget), p->minutes->widget);
    gtk_editable_set_max_width_chars(GTK_EDITABLE(p->minutes->entry), 2);

    if (! clock) gtk_box_append(GTK_BOX(p->widget), gtk_label_new(":"));

    p->seconds = ui_int_picker_new(mem, 0, 59, 0);
    gtk_box_append(GTK_BOX(p->widget), p->seconds->widget);
    gtk_editable_set_max_width_chars(GTK_EDITABLE(p->seconds->entry), 2);
    gtk_widget_set_visible(p->seconds->widget, !clock);

    return p;
}

// =============================================================================
// Date Picker
// =============================================================================
String ui_date_picker_get_date_str (UiDatePicker *p, Mem *mem, Bool nul_terminated) {
    AString a = astr_new(mem);
    astr_push_fmt(&a ,"%04u-%02u-%02u", p->year, p->month, p->day);
    if (nul_terminated) astr_push_byte(&a, 0);
    return astr_to_str(&a);
}

static Void ui_date_picker_on_date_change (UiDatePicker *p) {
    p->year  = gtk_calendar_get_year(GTK_CALENDAR(p->cal));
    p->month = 1 + gtk_calendar_get_month(GTK_CALENDAR(p->cal));
    p->day   = gtk_calendar_get_day(GTK_CALENDAR(p->cal));

    tmem_new(tm);
    String str = ui_date_picker_get_date_str(p, tm, true);
    gtk_label_set_label(GTK_LABEL(p->widget->label), str.data);
}

UiDatePicker *ui_date_picker_new (Mem *mem) {
    Auto p = mem_new(mem, UiDatePicker);

    p->widget = ui_popover_new(mem, 0, "_", false, false, false);
    gtk_widget_add_css_class(p->widget->widget, "kronomi-date-picker-button");
    gtk_widget_add_css_class(p->widget->popover, "kronomi-date-picker-popover");

    p->cal = gtk_calendar_new();
    gtk_box_append(GTK_BOX(p->widget->box), p->cal);

    //
    // listen
    //
    g_signal_connect(p->cal, "day-selected", G_CALLBACK(+[](GtkWidget *widget, gpointer data){
        ui_date_picker_on_date_change(static_cast<UiDatePicker*>(data));
    }), p);

    ui_date_picker_on_date_change(p);
    return p;
}

// =============================================================================
// Day Picker
// =============================================================================
U8 ui_day_picker_get_value (UiDayPicker *p) {
    U8 r = 0;

    for (U64 i = 0; i < 7; ++i) {
        U8 active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(p->buttons[i]));
        r |= active << i;
    }

    return r;
}

Void ui_day_picker_set_value (UiDayPicker *p, U8 flags) {
    for (U64 i = 0; i < 7; ++i) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p->buttons[i]), flags & (1 << i));
    }
}

UiDayPicker *ui_day_picker_new (Mem *mem, U8 flags) {
    Auto p = mem_new(mem, UiDayPicker);

    p->widget = ui_button_group_new(false, true);
    gtk_widget_add_css_class(p->widget, "kronomi-day-picker");

    CString labels[] = { tr("Sun"), tr("Mon"), tr("Tue"), tr("Wed"), tr("Thu"), tr("Fri"), tr("Sat") };
    for (U64 i = 0; i < 7; ++i) {
        p->buttons[i] = gtk_toggle_button_new_with_label(labels[i]);
        gtk_box_append(GTK_BOX(p->widget), p->buttons[i]);
    }

    ui_day_picker_set_value(p, flags);
    return p;
}

// =============================================================================
// Shortcut Picker
// =============================================================================
static Bool ui_on_shortcut_key_pressed (GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer data) {
    Auto p = static_cast<UiShortcutPicker*>(data);

    if (! p->listening) return GDK_EVENT_PROPAGATE;

    switch(keyval) {
    case GDK_KEY_Shift_L:
    case GDK_KEY_Shift_R:
    case GDK_KEY_Control_L:
    case GDK_KEY_Control_R:
    case GDK_KEY_Alt_L:
    case GDK_KEY_Alt_R:
    case GDK_KEY_Meta_L:
    case GDK_KEY_Meta_R:
    case GDK_KEY_Super_L:
    case GDK_KEY_Super_R:
    case GDK_KEY_Hyper_L:
    case GDK_KEY_Hyper_R:
        return GDK_EVENT_STOP;
    default: break;
    }

    if (keyval == GDK_KEY_BackSpace) {
        p->selected = false;
        gtk_button_set_label(GTK_BUTTON(p->widget), "Change Shortcut");
    } else {
        p->selected = true;
        p->keycode = keycode;
        p->keyval = keyval;
        p->state = state;
        Char *shortcut = gtk_accelerator_name(keyval, state);
        gtk_button_set_label(GTK_BUTTON(p->widget), shortcut);
        g_free(shortcut);
    }

    gtk_widget_remove_controller(p->widget, p->controller);
    p->listening = false;
    return GDK_EVENT_STOP;
}

UiShortcutPicker *ui_shortcut_picker_new (Mem *mem) {
    Auto p = mem_new(mem, UiShortcutPicker);

    p->widget = gtk_button_new_with_label("Change Shortcut");
    gtk_widget_add_css_class(p->widget, "kronomi-shortcut-picker");
    gtk_widget_set_hexpand(p->widget, false);

    //
    // listen
    //
    g_signal_connect(p->widget, "clicked", G_CALLBACK(+[](GtkWidget *widget, gpointer data){
        Auto p = static_cast<UiShortcutPicker*>(data);

        if (p->listening) return;

        p->listening = true;
        gtk_button_set_label(GTK_BUTTON(p->widget), "Type shortcut (backspace to reset)");

        p->controller = gtk_event_controller_key_new();
        gtk_widget_add_controller(p->widget, p->controller);
        g_signal_connect(p->controller, "key-pressed", G_CALLBACK(ui_on_shortcut_key_pressed), p);
    }), p);

    return p;
}

// =============================================================================
// Color picker
// =============================================================================
UiColorPicker *ui_color_picker_new (Mem *mem) {
    Auto c = mem_new(mem, UiColorPicker);
    GtkColorDialog *dialog = gtk_color_dialog_new();
    c->widget = gtk_color_dialog_button_new(dialog);
    return c;
}

// =============================================================================
// Image
// =============================================================================
UiImage *ui_image_new (Mem *mem, String path, U64 width) {
    if (width == 0) width = 100;

    Auto i = mem_new(mem, UiImage);
    i->path = path;
    i->width = width;

    i->widget = gtk_scrolled_window_new();
    gtk_widget_add_css_class(i->widget, "kronomi-image");
    gtk_widget_set_hexpand(i->widget, false);
    gtk_widget_set_halign(i->widget, GTK_ALIGN_START);
    gtk_widget_set_size_request(i->widget, width, width);

    tmem_new(tm);
    GtkWidget *pic = gtk_picture_new_for_filename(cstr(tm, path));
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(i->widget), pic);
    gtk_picture_set_can_shrink(GTK_PICTURE(pic), true);
    gtk_widget_set_halign(pic, GTK_ALIGN_START);

    //
    // listen
    //
    GtkGesture *click = gtk_gesture_click_new();
    gtk_widget_add_controller(pic, GTK_EVENT_CONTROLLER(click));
    g_signal_connect(click, "pressed", G_CALLBACK(+[](GtkGestureClick *gesture, int n_press, F64 x, F64 y, gpointer data){
        Auto i = static_cast<UiImage*>(data);
        ui_open_file_uri(i->path);
    }), i);

    return i;
}

// =============================================================================
// Expander
// =============================================================================
Void ui_expander_toggle (UiExpander *e) {
    e->open = !e->open;

    if (e->open) {
        gtk_widget_add_css_class(e->widget, "open");
    } else {
        gtk_widget_remove_css_class(e->widget, "open");
    }

    gtk_widget_set_visible(e->content, e->open);
    gtk_image_set_from_icon_name(GTK_IMAGE(e->button->icon), e->open ? "kronomi-pan-down-symbolic" : "kronomi-pan-right-symbolic");
}

UiExpander *ui_expander_new (Mem *mem, CString title) {
    Auto e = mem_new(mem, UiExpander);

    e->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(e->widget, "kronomi-expander");
    gtk_widget_set_hexpand(e->widget, true);

    e->button = ui_button_new(mem, "kronomi-pan-right-symbolic", title, true, true, false);
    gtk_box_append(GTK_BOX(e->widget), e->button->widget);
    gtk_widget_set_halign(e->button->label, GTK_ALIGN_START);

    e->content = ui_box_new(true);
    gtk_box_append(GTK_BOX(e->widget), e->content);
    gtk_widget_set_visible(e->content, false);
    gtk_widget_add_css_class(e->content, "kronomi-body");

    //
    // listen
    //
    g_signal_connect(e->button->widget, "clicked", G_CALLBACK(+[](GtkWidget *widget, gpointer data){
        Auto e = static_cast<UiExpander*>(data);
        ui_expander_toggle(e);
    }), e);

    return e;
}

// =============================================================================
// Dropdown
// =============================================================================
Void ui_dropdown_select (UiDropdown *d, U64 selection) {
    tmem_new(tm);
    U64 idx = array_find(&d->list, [&](U64 it){ return it == selection; });
    d->selection = selection;
    d->selection_idx = idx;
    gtk_label_set_label(GTK_LABEL(d->widget->label), cstr(tm, array_get(&d->display_list, idx)));
}

UiDropdown *ui_dropdown_new (Mem *mem, U64 selection, Slice<U64> list, Slice<String> display_list) {
    Auto d = mem_new(mem, UiDropdown);
    d->selection = selection;
    d->list = list;
    d->display_list = display_list;

    d->widget = ui_popover_new(mem, 0, "", false, true, false);
    gtk_widget_add_css_class(d->widget->widget, "kronomi-dropdown-button");
    gtk_widget_add_css_class(d->widget->popover, "kronomi-dropdown-popover");

    array_iter (val, &list) {
        tmem_new(tm);

        String display_string = array_get(&display_list, ARRAY_IDX);
        Auto button = ui_button_new(mem, 0, cstr(tm, display_string), true, true, true);

        gtk_box_append(GTK_BOX(d->widget->box), button->widget);
        gtk_widget_set_halign(button->label, GTK_ALIGN_START);

        if (val == selection) gtk_label_set_label(GTK_LABEL(d->widget->label), cstr(tm, display_string));

        g_signal_connect(button->widget, "clicked", G_CALLBACK(+[](GtkWidget *widget, gpointer data){
            Auto d = static_cast<UiDropdown*>(data);

            U64 idx = 0;
            GtkWidget *iter = gtk_widget_get_first_child(gtk_widget_get_parent(widget));
            while (iter) {
                if (iter == widget) break;
                iter = gtk_widget_get_next_sibling(iter);
                idx++;
            }

            ui_dropdown_select(d, array_get(&d->list, idx));
            gtk_menu_button_popdown(GTK_MENU_BUTTON(d->widget->widget));
        }), d);
    }

    //
    // listen
    //
    Auto scroll = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
    gtk_widget_add_controller(d->widget->widget, scroll);
    g_signal_connect(scroll, "scroll", G_CALLBACK(+[](GtkEventControllerScroll *controller, F64 dx, F64 dy, Void *data){
        Auto d = static_cast<UiDropdown*>(data);

        if (dy > 0) {
            U64 idx = d->selection_idx < d->list.count-1 ? d->selection_idx+1 : d->selection_idx;
            ui_dropdown_select(d, array_get(&d->list, idx));
        } else {
            U64 idx = d->selection_idx > 0 ? d->selection_idx-1 : d->selection_idx;
            ui_dropdown_select(d, array_get(&d->list, idx));
        }

        return true;
    }), d);

    return d;
}

// =============================================================================
// Row
// =============================================================================
GtkWidget *ui_row_group_new () {
    Auto g = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(g, "kronomi-row-group");
    return g;
}

UiRow *ui_row_new (Mem *mem, String label, GtkWidget *subwidget) {
    tmem_new(tm);

    Auto r = mem_new(mem, UiRow);

    r->widget = ui_box_new(false);
    gtk_widget_add_css_class(r->widget, "kronomi-row");

    if (label.data) {
        r->label = gtk_label_new(cstr(tm, label));
        gtk_box_append(GTK_BOX(r->widget), r->label);
        gtk_widget_set_hexpand(r->label, true);
        gtk_widget_set_halign(r->label, GTK_ALIGN_START);
    }

    gtk_box_append(GTK_BOX(r->widget), subwidget);
    if (label.data) gtk_widget_set_halign(subwidget, GTK_ALIGN_END);

    return r;
}

// =============================================================================
// Card
// =============================================================================
UiCard *ui_card_new (Mem *mem) {
    Auto c = mem_new(mem, UiCard);

    c->widget = ui_box_new(true);
    gtk_widget_add_css_class(c->widget, "kronomi-card");
    gtk_box_set_spacing(GTK_BOX(c->widget), 0);
    gtk_widget_set_vexpand(c->widget, false);
    gtk_widget_set_valign(c->widget, GTK_ALIGN_START);

    c->header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, SPACING);
    gtk_widget_add_css_class(c->header, "kronomi-card-header");
    gtk_widget_add_css_class(c->header, "frame");
    gtk_box_append(GTK_BOX(c->widget), c->header);

    c->left_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append(GTK_BOX(c->header), c->left_header);
    gtk_widget_add_css_class(c->left_header, "kronomi-card-left-header");
    gtk_widget_set_hexpand(c->left_header, true);
    gtk_widget_set_halign(c->left_header, GTK_ALIGN_START);

    c->autohide_header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append(GTK_BOX(c->header), c->autohide_header);
    gtk_widget_add_css_class(c->autohide_header, "kronomi-card-autohide-header");
    gtk_widget_add_css_class(c->autohide_header, "kronomi-invisible");
    gtk_widget_set_halign(c->autohide_header, GTK_ALIGN_END);

    c->body = gtk_box_new(GTK_ORIENTATION_VERTICAL, SPACING);
    gtk_widget_add_css_class(c->body, "kronomi-card-body");
    gtk_box_append(GTK_BOX(c->widget), c->body);
    gtk_widget_set_hexpand(c->body, true);
    gtk_widget_set_vexpand(c->widget, false);
    gtk_widget_set_valign(c->widget, GTK_ALIGN_START);

    //
    // listen
    //
    GtkEventController *focus = gtk_event_controller_focus_new();
    GtkEventController *motion = gtk_event_controller_motion_new();
    gtk_widget_add_controller(c->widget, focus);
    gtk_widget_add_controller(c->widget, motion);
    g_signal_connect(focus, "enter", G_CALLBACK(+[](GtkEventController *controller, F64 x, F64 y, gpointer data){
        Auto c = static_cast<UiCard*>(data);
        c->has_focus = true;
        gtk_widget_remove_css_class(c->autohide_header, "kronomi-invisible");

        g_idle_add([](Void *data){
            Auto c = reinterpret_cast<UiCard*>(data);
            ui_scroll_to_widget(c->widget);
            return 0;
        }, c);
    }), c);
    g_signal_connect(focus, "leave", G_CALLBACK(+[](GtkEventController *controller, F64 x, F64 y, gpointer data){
        Auto c = static_cast<UiCard*>(data);
        c->has_focus = false;
        if (! c->has_mouse) gtk_widget_add_css_class(c->autohide_header, "kronomi-invisible");
    }), c);
    g_signal_connect(motion, "enter", G_CALLBACK(+[](GtkEventController *controller, F64 x, F64 y, gpointer data){
        Auto c = static_cast<UiCard*>(data);
        c->has_mouse = true;
        gtk_widget_remove_css_class(c->autohide_header, "kronomi-invisible");
    }), c);
    g_signal_connect(motion, "leave", G_CALLBACK(+[](GtkEventController *controller, gpointer data){
        Auto c = static_cast<UiCard*>(data);
        c->has_mouse = false;
        if (! c->has_focus) gtk_widget_add_css_class(c->autohide_header, "kronomi-invisible");
    }), c);

    return c;
}

// =============================================================================
// Scrollbox
// =============================================================================
UiScrollbox *ui_scrollbox_new (Mem *mem, Bool vertical) {
    Auto s = mem_new(mem, UiScrollbox);

    s->widget = gtk_scrolled_window_new();
    gtk_widget_add_css_class(s->widget, "kronomi-scrollbox");
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(s->widget), true);
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(s->widget), true);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(s->widget), (vertical ? GTK_POLICY_NEVER : GTK_POLICY_AUTOMATIC), (vertical ? GTK_POLICY_AUTOMATIC : GTK_POLICY_NEVER));
    gtk_widget_set_vexpand(s->widget, false);
    gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(s->widget), true);

    s->viewport = gtk_viewport_new(0, 0);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(s->widget), s->viewport);

    s->box = gtk_box_new(vertical ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL, SPACING);
    gtk_viewport_set_child(GTK_VIEWPORT(s->viewport), s->box);

    if (! vertical) {
        Auto controller = gtk_event_controller_legacy_new();
        gtk_event_controller_set_propagation_phase(controller, GTK_PHASE_CAPTURE);
        gtk_widget_add_controller(s->widget, controller);
        g_signal_connect(controller, "event", G_CALLBACK(+[](GtkEventControllerLegacy *controller, GdkEvent *event, gpointer data){
            if (gdk_event_get_event_type(event) != GDK_SCROLL) return false;

            GdkModifierType state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(controller));
            if (! (state & GDK_CONTROL_MASK)) return false;

            Auto s      = static_cast<UiScrollbox*>(data);
            Auto dir    = gdk_scroll_event_get_direction(event);
            Auto adjust = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(s->widget));
            F64 inc     = gtk_adjustment_get_step_increment(adjust);

            if (dir == GDK_SCROLL_UP) {
                gtk_adjustment_set_value(adjust, gtk_adjustment_get_value(adjust) - inc);
                return true;
            } else if (dir == GDK_SCROLL_DOWN) {
                gtk_adjustment_set_value(adjust, gtk_adjustment_get_value(adjust) + inc);
                return true;
            } else {
                return false;
            }
        }), s);
    }

    return s;
}

// =============================================================================
// Lazy Scrollbox
// =============================================================================
const U64 LAZY_SCROLLBOX_PAGE_SIZE = 20; // @todo Should be pulled from global settings.

static Void lazy_scrollbox_show_more (UiLazyScrollbox *s, Bool first_time) {
    if (s->all_shown) return;

    if (s->show_more_button) gtk_widget_unparent(s->show_more_button);

    Auto last_child = gtk_widget_get_last_child(s->widget->box);
    s->scroll_to = 0;

    for (U64 i = 0; i < LAZY_SCROLLBOX_PAGE_SIZE; ++i) {
        GtkWidget *w = s->gen(s->gen_data);
        if (! w) { s->all_shown = true; break; }
        if (! s->scroll_to) s->scroll_to = w;
        gtk_box_append(GTK_BOX(s->widget->box), w);
    }

    if (! s->all_shown) {
        s->show_more_button = ui_button_new(s->mem, 0, tr("Show more..."), true, false, false)->widget;
        gtk_box_append(GTK_BOX(s->widget->box), s->show_more_button);

        g_signal_connect(s->show_more_button, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
            Auto s = static_cast<UiLazyScrollbox*>(data);
            lazy_scrollbox_show_more(s, false);
        }), s);
    }

    if (! first_time) {
        if (s->scroll_to) {
            gtk_widget_child_focus(s->scroll_to, GTK_DIR_TAB_FORWARD);
        } else if (last_child) {
            gtk_widget_child_focus(last_child, GTK_DIR_TAB_FORWARD);
        }
    }
}

UiLazyScrollbox *ui_lazy_scrollbox_new (Mem *mem, Bool vertical, UiLazyScrollboxGen gen, Void *gen_data) {
    Auto s = mem_new(mem, UiLazyScrollbox);
    s->mem = mem;
    s->widget = ui_scrollbox_new(mem, vertical);
    s->gen = gen;
    s->gen_data = gen_data;
    lazy_scrollbox_show_more(s, true);
    return s;
}

// =============================================================================
// Text View
// =============================================================================
UiTextView *ui_text_view_new (Mem *mem) {
    Auto t = mem_new(mem, UiTextView);

    t->widget = ui_scrollbox_new(mem, true);
    gtk_widget_set_hexpand(t->widget->widget, false);
    gtk_widget_add_css_class(t->widget->widget, "kronomi-text-view-scroll");

    t->entry = gtk_text_view_new();
    gtk_box_append(GTK_BOX(t->widget->box), t->entry);
    gtk_widget_add_css_class(t->entry, "kronomi-text-view");
    gtk_widget_add_css_class(t->entry, "frame");
    gtk_text_view_set_accepts_tab(GTK_TEXT_VIEW(t->entry), false);
    gtk_widget_set_vexpand(t->entry, true);

    //
    // listen
    //
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(t->entry));
    GtkEventController *keyc = gtk_event_controller_key_new();
    gtk_widget_add_controller(t->entry, keyc);
    Auto on_key_pressed = +[](GtkEventControllerKey *, guint keyval, guint keycode, GdkModifierType state, gpointer data){
        Auto t = static_cast<UiTextView*>(data);

        if (! (state & GDK_CONTROL_MASK)) return 0;

        Int n = 40;
        Int w = gtk_widget_get_width(t->entry);
        Int h = gtk_widget_get_height(t->entry);

        switch (keyval) {
        case GDK_KEY_h: gtk_widget_set_size_request(t->entry, w+n, h); return 1;
        case GDK_KEY_j: gtk_widget_set_size_request(t->entry, w, h+n); return 1;
        case GDK_KEY_k: gtk_widget_set_size_request(t->entry, w, max(0, h-n)); return 1;
        case GDK_KEY_l: gtk_widget_set_size_request(t->entry, max(0, w-n), h); return 1;
        default: return 0;
        }
    };
    Auto on_cursor_moved = +[](GtkTextBuffer *, GtkTextIter *, GtkTextMark *, gpointer data){
        Auto t = static_cast<UiTextView*>(data);

        GtkTextView *view = GTK_TEXT_VIEW(t->entry);
        GtkTextBuffer *buf = gtk_text_view_get_buffer(view);
        GtkTextMark *insert = gtk_text_buffer_get_insert(buf);
        if (! insert) return;

        GtkTextIter iter; gtk_text_buffer_get_iter_at_mark(buf, &iter, insert);
        GdkRectangle rect; gtk_text_view_get_iter_location(view, &iter, &rect);

        gint idx = gtk_text_iter_get_offset(&iter);
        if (t->cursor_idx == idx) return;
        t->cursor_idx = idx;

        GtkAdjustment *adjust = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(t->widget->widget));
        F64 value = gtk_adjustment_get_value(adjust);
        F64 page  = gtk_adjustment_get_page_size(adjust);
        F64 lower = gtk_adjustment_get_lower(adjust);
        F64 upper = gtk_adjustment_get_upper(adjust);

        F64 new_value = value;
        if (rect.y < value) {
            new_value = rect.y;
        } else if (rect.y + 2*rect.height > value + page) {
            new_value = rect.y + 2*rect.height - page;
        }

        if (new_value < lower)        new_value = lower;
        if (new_value > upper - page) new_value = upper - page;

        gtk_adjustment_set_value(adjust, new_value);
        return;
    };
    g_signal_connect(keyc, "key-pressed", G_CALLBACK(on_key_pressed), t);
    g_signal_connect(buffer, "mark-set", G_CALLBACK(on_cursor_moved), t);

    return t;
}

String ui_text_view_get_text (Mem *mem, UiTextView *tv) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv->entry));
    GtkTextIter start, end;
    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_get_end_iter(buffer, &end);
    Char *buf = gtk_text_buffer_get_text(buffer, &start, &end, 0);
    Auto result = str_copy(mem, str(buf));
    g_free(buf);
    return result;
}

Void ui_text_view_set_text (UiTextView *tv, String text) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv->entry));
    if (text.count) gtk_text_buffer_set_text(buffer, text.data, text.count);
}
