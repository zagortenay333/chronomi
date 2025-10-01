#pragma once

#include <gtk/gtk.h>
#include "util/core.h"
#include "util/array.h"
#include "util/string.h"
#include "os/time.h"

// =============================================================================
// Switch
// =============================================================================
GtkWidget *ui_switch_new (Bool);

// =============================================================================
// Box
// =============================================================================
GtkWidget *ui_box_new (Bool vertical);

// =============================================================================
// Buttons
// =============================================================================
struct UiButton {
    GtkWidget *widget;
    GtkWidget *label;
    GtkWidget *icon;
};

UiButton  *ui_button_new       (Mem *mem, CString icon_name, CString label, Bool wide, Bool icon_after_label, Bool floating);
GtkWidget *ui_button_group_new (Bool, Bool);

// =============================================================================
// Checkbox
// =============================================================================
Bool ui_checkbox_checked (GtkWidget *);

// =============================================================================
// File Picker
// =============================================================================
struct UiFilePicker {
    GtkWidget *widget;
    GtkWidget *entry;
    Bool multiple;
    Bool dirs;
    String start_dir;
};

// If multiple is true, the entry text will be a list separated with the '|' char.
UiFilePicker *ui_file_picker_new (Mem *mem, CString, Bool multiple, Bool dirs, String start_dir);

// =============================================================================
// Confirm Dialog
// =============================================================================
struct UiConfirmDialog {
    Void (*fn)(Void *);
    Void *fn_data;
};

Void ui_confirm_dialog_new (CString msg, Void (*)(Void*), Void *);

// =============================================================================
// Popover
// =============================================================================
struct UiPopover {
    GtkWidget *widget;
    GtkWidget *box;
    GtkWidget *popover;
    GtkWidget *label;
    GtkWidget *icon;
};

struct UiPopoverConfirm {
    UiPopover *widget;
    GtkWidget *confirm;
};

UiPopover        *ui_popover_new         (Mem *mem, CString icon_name, CString label, Bool wide, Bool icon_after_label, Bool floating);
Void              ui_popover_set_info    (UiPopover *, Mem *mem, String text);
UiPopoverConfirm *ui_popover_confirm_new (Mem *mem, CString icon_name, CString label, Bool wide, Bool icon_after_label, Bool floating);

// =============================================================================
// Int Picker
// =============================================================================
struct UiIntPicker {
    GtkWidget *widget;
    GtkWidget *entry;
    UiPopover *info;
    I64 min;
    I64 max;
    I64 init;
    I64 val;
    Bool valid;
};

UiIntPicker *ui_int_picker_new              (Mem *mem, I64 min, I64 max, I64 initial);
Void         ui_int_picker_set_value        (UiIntPicker *p, I64 val);
Void         ui_int_picker_on_value_changed (UiIntPicker *p);

// =============================================================================
// Time Picker
// =============================================================================
struct UiTimePicker {
    GtkWidget *widget;
    UiIntPicker *hours;
    UiIntPicker *minutes;
    UiIntPicker *seconds;
};

UiTimePicker *ui_time_picker_new (Mem *mem, Bool);
Millisec      ui_time_picker_get (UiTimePicker *);
Void          ui_time_picker_set (UiTimePicker *, Millisec);

// =============================================================================
// Date Picker
// =============================================================================
struct UiDatePicker {
    UiPopover *widget;
    GtkWidget *cal;
    U32 year;
    U32 month;
    U32 day;
};

UiDatePicker *ui_date_picker_new          (Mem *mem);
String        ui_date_picker_get_date_str (UiDatePicker *p, Mem *mem, Bool nul_terminated);

// =============================================================================
// Day Picker
// =============================================================================
struct UiDayPicker {
    GtkWidget *widget;
    GtkWidget *buttons[7];
};

UiDayPicker *ui_day_picker_new       (Mem *, U8);
U8           ui_day_picker_get_value (UiDayPicker *);
Void         ui_day_picker_set_value (UiDayPicker *, U8);

// =============================================================================
// Shortcut Picker
// =============================================================================
struct UiShortcutPicker {
    GtkWidget *widget;
    GtkEventController *controller;
    Bool listening;
    Bool selected;
    guint keyval;
    guint keycode;
    GdkModifierType state;
};

UiShortcutPicker *ui_shortcut_picker_new (Mem *mem);

// =============================================================================
// Color picker
// =============================================================================
struct UiColorPicker {
    GtkWidget *widget;
    GdkRGBA color;
};

UiColorPicker *ui_color_picker_new (Mem *mem);

// =============================================================================
// Image
// =============================================================================
struct UiImage {
    GtkWidget *widget;
    String path;
    U64 width;
};

UiImage *ui_image_new (Mem *mem, String path, U64 width);

// =============================================================================
// Expander
// =============================================================================
struct UiExpander {
    GtkWidget *widget;
    UiButton *button;
    GtkWidget *content;
    Bool open;
};

UiExpander *ui_expander_new    (Mem *mem, CString title);
Void        ui_expander_toggle (UiExpander *e);

// =============================================================================
// Dropdown
// =============================================================================
struct UiDropdown {
    UiPopover *widget;
    U64 selection;
    U64 selection_idx;
    Slice<U64> list;
    Slice<String> display_list; // Parallel to list.
};

UiDropdown *ui_dropdown_new    (Mem *mem, U64 selection, Slice<U64> list, Slice<String> display_list);
Void        ui_dropdown_select (UiDropdown *d, U64 selection);

// =============================================================================
// Row
// =============================================================================
struct UiRow {
    GtkWidget *widget;
    GtkWidget *label;
    GtkWidget *subwidget;
};

UiRow     *ui_row_new       (Mem *mem, String label, GtkWidget *subwidget);
GtkWidget *ui_row_group_new ();

// =============================================================================
// Card
// =============================================================================
struct UiCard {
    GtkWidget *widget;
    GtkWidget *header;
    GtkWidget *left_header;
    GtkWidget *autohide_header;
    GtkWidget *body;
    Bool has_mouse;
    Bool has_focus;
};

UiCard *ui_card_new (Mem *mem);

// =============================================================================
// Scrollbox
// =============================================================================
struct UiScrollbox {
    GtkWidget *widget;
    GtkWidget *viewport;
    GtkWidget *box;
};

UiScrollbox *ui_scrollbox_new (Mem *mem, Bool vertical);

// =============================================================================
// Lazy Scrollbox
// =============================================================================
using UiLazyScrollboxGen = GtkWidget *(*)(Void*);

struct UiLazyScrollbox {
    Mem *mem;
    UiScrollbox *widget;
    GtkWidget *show_more_button;
    UiLazyScrollboxGen gen;
    Void *gen_data;
    Bool all_shown;
    GtkWidget *scroll_to;
};

UiLazyScrollbox *ui_lazy_scrollbox_new (Mem *mem, Bool vertical, UiLazyScrollboxGen, Void*);

// =============================================================================
// Text View
// =============================================================================
struct UiTextView {
    UiScrollbox *widget;
    GtkWidget *entry;
    Int cursor_idx;
};

UiTextView *ui_text_view_new      (Mem *);
String      ui_text_view_get_text (Mem *, UiTextView *);
Void        ui_text_view_set_text (UiTextView *, String);
