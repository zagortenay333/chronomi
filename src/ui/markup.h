#pragma once

#include <gtk/gtk.h>
#include "ui/widgets.h"
#include "util/core.h"
#include "core/markup.h"
#include "util/mem.h"
#include "util/string.h"

struct ParagraphContext;

struct UiMarkup {
    Mem *mem;
    GtkWidget *widget;
    String text;
    MarkupAst *ast;
    Void (*on_tag_clicked)(String tag);
    Map<MarkupAst*, GtkWidget*> ast_to_widget;
    ParagraphContext *paragraph_context;
};

struct UiMarkupEditor {
    Mem *mem;
    UiTextView *text_view; // unparented
    UiScrollbox *markup_scroll; // unparented
    UiMarkup *markup;
    Bool preview_reactive;
    Void (*on_render)(UiMarkupEditor*);
    Int cursor_idx;
    Arena *markup_arena;
    GtkWidget *highlight;
    Int timeout_id;
};

// Wrapper around UiMarkupEditor that puts
// the entry and preivew inside of UiCard's.
struct UiMarkupEditor2 {
    UiMarkupEditor *ed;
    UiCard *entry_card;
    UiCard *markup_card;
};

UiMarkup        *ui_markup_new         (Mem *mem, String);
UiMarkup        *ui_markup_new         (Mem *mem, String, MarkupAst *);
UiMarkupEditor  *ui_markup_editor_new  (Mem *mem, String);
UiMarkupEditor  *ui_markup_editor_new  (Mem *mem, String, Void(*)(UiMarkupEditor*));
UiMarkupEditor2 *ui_markup_editor2_new (Mem *mem, String, CString, CString);
