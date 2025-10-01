#include "ui/flashcards.h"
#include "ui/widgets.h"
#include "ui/markup.h"
#include "ui/util.h"
#include "core/flashcards.h"
#include "core/config.h"
#include "os/fs.h"
#include "util/core.h"

struct Context {
    Applet pub;
    View *view;
    Flashcards *core;
    String settings_path;
};

static Context context;

static Void main_view_new ();
static Void flush_settings ();
static Void deck_view_do_search ();
static Void editor_view_new (FlashCard *);

// =============================================================================
// Flashcard widget
// =============================================================================
struct UiFlashcard {
    FlashCard *card;
    UiCard *widget;
};

static UiFlashcard *ui_flashcard_new (Mem *mem, FlashCard *card) {
    Auto w = mem_new(mem, UiFlashcard);
    w->card = card;
    w->widget = ui_card_new(mem);

    tmem_new(tm);

    CString in_parens = 0;
    switch (card->bucket) {
    case 0: in_parens = tr("every session"); break;
    case 1: in_parens = tr("every 2 sessions"); break;
    case 2: in_parens = tr("every 4 sessions"); break;
    case 3: in_parens = tr("every 8 sessions"); break;
    case 4: in_parens = tr("every 16 sessions"); break;
    case 5: in_parens = tr("every 32 sessions"); break;
    }

    String title = astr_fmt(tm, "#%lu (%s)%c", card->bucket, in_parens, 0);
    GtkWidget *bucket = gtk_label_new(title.data);
    gtk_box_append(GTK_BOX(w->widget->left_header), bucket);

    UiButton *edit_button = ui_button_new(mem, "kronomi-edit-symbolic", 0, false, false, true);
    gtk_box_append(GTK_BOX(w->widget->autohide_header), edit_button->widget);

    UiPopoverConfirm *delete_button = ui_popover_confirm_new(mem, "kronomi-trash-symbolic", 0, false, false, true);
    gtk_box_append(GTK_BOX(w->widget->autohide_header), delete_button->widget->widget);

    UiMarkup *q = ui_markup_new(mem, card->question);
    gtk_box_append(GTK_BOX(w->widget->body), q->widget);

    UiExpander *expander = ui_expander_new(mem, tr("Answer"));
    gtk_box_append(GTK_BOX(w->widget->body), expander->widget);
    UiMarkup *a = ui_markup_new(mem, card->answer);
    gtk_box_append(GTK_BOX(expander->content), a->widget);

    //
    // listen
    //
    g_signal_connect(edit_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto w = reinterpret_cast<UiFlashcard*>(data);
        editor_view_new(w->card);
    }), w);
    g_signal_connect(delete_button->confirm, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto w = reinterpret_cast<UiFlashcard*>(data);
        fc_del_card(context.core, w->card);
        fc_flush_deck(context.core);
        main_view_new();
    }), w);

    return w;
}

// =============================================================================
// Exam View
// =============================================================================
struct ViewExam {
    View base;
    UiIntPicker *session;
    GtkWidget *remaining;
    UiFlashcard *shown_card;
    GtkWidget *correct_button;
    GtkWidget *wrong_button;
    Array<FlashCard*> cards;
};

static Void exam_view_collect_cards () {
    Auto view = reinterpret_cast<ViewExam*>(context.view);

    view->cards.count = 0;

    array_iter (card, &context.core->cards) {
        U64 days = 1lu << min(5lu, card->bucket);
        U64 session = context.core->session;
        if ((session % days) == 0) array_push(&view->cards, card);
    }

    array_reverse(&view->cards);
}

static void exam_view_show_next_card () {
    tmem_new(tm);
    Auto view = reinterpret_cast<ViewExam*>(context.view);

    if (view->shown_card) gtk_widget_unparent(view->shown_card->widget->widget);
    view->shown_card = 0;

    String label = astr_fmt(tm, "%lu%c", view->cards.count, 0);
    gtk_label_set_text(GTK_LABEL(view->remaining), label.data);

    if (view->cards.count) {
        view->shown_card = ui_flashcard_new(context.view->arena, array_pop(&view->cards));
        gtk_box_append(GTK_BOX(context.view->box_right), view->shown_card->widget->widget);
        gtk_widget_set_visible(view->shown_card->widget->autohide_header, false);
        gtk_widget_set_visible(view->correct_button, true);
        gtk_widget_set_visible(view->wrong_button, true);
    } else {
        gtk_widget_set_visible(view->correct_button, false);
        gtk_widget_set_visible(view->wrong_button, false);
    }
}

static Void exam_view_new () {
    if (context.view) ui_view_destroy(context.view);

    Auto view = ui_view_new<ViewExam>(4*KB, context.pub.roots.box, true, true);
    context.view = &view->base;
    array_init(&view->cards, context.view->arena);
    gtk_widget_set_visible(view->base.box_left_scroll->widget, false);
    defer { exam_view_collect_cards(); exam_view_show_next_card(); };

    //
    // session row
    //
    GtkWidget *group = ui_row_group_new();
    gtk_box_append(GTK_BOX(view->base.box_right), group);

    view->session = ui_int_picker_new(context.view->arena, 1, 32, context.core->session);
    UiRow *row = ui_row_new(context.view->arena, str(tr("Session")), view->session->widget);
    gtk_box_append(GTK_BOX(group), row->widget);

    //
    // remaining cards row
    //
    view->remaining = gtk_label_new("32");
    row = ui_row_new(context.view->arena, str(tr("Remaining cards")), view->remaining);
    gtk_box_append(GTK_BOX(group), row->widget);

    //
    // buttons
    //
    GtkWidget *button_group = ui_button_group_new(false, true);
    gtk_box_append(GTK_BOX(view->base.box_right), button_group);

    UiButton *close_button = ui_button_new(context.view->arena, 0, tr("Close"), true, false, false);
    gtk_box_append(GTK_BOX(button_group), close_button->widget);

    UiButton *correct_button = ui_button_new(context.view->arena, 0, tr("Correct"), true, false, false);
    gtk_box_append(GTK_BOX(button_group), correct_button->widget);
    gtk_widget_add_css_class(correct_button->widget, "kronomi-success-color");
    view->correct_button = correct_button->widget;

    UiButton *wrong_button = ui_button_new(context.view->arena, 0, tr("Wrong"), true, false, false);
    gtk_box_append(GTK_BOX(button_group), wrong_button->widget);
    gtk_widget_add_css_class(wrong_button->widget, "kronomi-error-color");
    view->wrong_button = wrong_button->widget;

    //
    // listen
    //
    g_signal_connect(GTK_EDITABLE(view->session->entry), "changed", G_CALLBACK(+[](GtkWidget*, Void *){
        Auto view = reinterpret_cast<ViewExam*>(context.view);
        context.core->session = view->session->val;
        exam_view_collect_cards();
        exam_view_show_next_card();
    }), 0);
    g_signal_connect(close_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void *){
        context.core->session++;
        if (context.core->session > 32) context.core->session = 1;
        fc_flush_deck(context.core);
        main_view_new();
    }), 0);
    g_signal_connect(correct_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void *){
        Auto view = reinterpret_cast<ViewExam*>(context.view);
        if (view->shown_card) {
            view->shown_card->card->bucket++;
            if (view->shown_card->card->bucket > 5) view->shown_card->card->bucket = 5;
        }
        exam_view_show_next_card();
    }), 0);
    g_signal_connect(wrong_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void *){
        Auto view = reinterpret_cast<ViewExam*>(context.view);
        if (view->shown_card) view->shown_card->card->bucket = 0;
        exam_view_show_next_card();
    }), 0);
}

// =============================================================================
// Decks View
// =============================================================================
struct ViewDecks {
    View base;
    GtkWidget *entry;
    UiFilePicker *import_paths;
    UiFilePicker *export_path;
    GtkWidget *delete_checkbox;
    UiScrollbox *decks;
    Array<FlashDeck*> search_results;
};

static GtkWidget *deck_view_card_new (ViewDecks *view, FlashDeck *deck) {
    tmem_new(tm);

    UiCard *card = ui_card_new(context.view->arena);

    GtkWidget *checkbox = gtk_check_button_new();
    gtk_box_append(GTK_BOX(card->left_header), checkbox);
    gtk_check_button_set_active(GTK_CHECK_BUTTON(checkbox), deck->active);

    UiButton *edit_button = ui_button_new(context.view->arena, "kronomi-edit-symbolic", 0, false, false, true);
    gtk_box_append(GTK_BOX(card->autohide_header), edit_button->widget);

    UiPopoverConfirm *delete_button = ui_popover_confirm_new(context.view->arena, "kronomi-trash-symbolic", 0, false, false, true);
    gtk_box_append(GTK_BOX(card->autohide_header), delete_button->widget->widget);

    GtkWidget *entry = gtk_entry_new();
    gtk_box_append(GTK_BOX(card->body), entry);
    gtk_editable_set_text(GTK_EDITABLE(entry), cstr(tm, deck->path));
    gtk_editable_set_position(GTK_EDITABLE(entry), -1);
    gtk_editable_set_editable(GTK_EDITABLE(entry), false);

    //
    // listen
    //
    g_signal_connect(edit_button->widget, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto deck = reinterpret_cast<FlashDeck*>(data);
        ui_open_file_uri(deck->path);
    }), deck);
    g_signal_connect(delete_button->confirm, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto deck = reinterpret_cast<FlashDeck*>(data);
        fc_del_deck(context.core, deck);
        flush_settings();
        deck_view_do_search();
    }), deck);
    g_signal_connect(checkbox, "toggled", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto deck = reinterpret_cast<FlashDeck*>(data);
        fc_mark_active_deck(context.core, deck);
        fc_load_deck(context.core, 0);
        flush_settings();
        main_view_new();
    }), deck);

    return card->widget;
}

static Void deck_view_do_search () {
    Auto view = reinterpret_cast<ViewDecks*>(context.view);

    if (view->decks) gtk_widget_unparent(view->decks->widget);

    array_init(&view->search_results, context.view->arena);

    view->decks = ui_scrollbox_new(context.view->arena, true);
    gtk_box_append(GTK_BOX(view->base.box_right), view->decks->widget);
    gtk_widget_set_hexpand(view->decks->box, true);
    gtk_widget_set_vexpand(view->decks->box, true);
    gtk_widget_set_halign(view->decks->box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(view->decks->box, GTK_ALIGN_START);

    String needle = str(gtk_editable_get_text(GTK_EDITABLE(view->entry)));
    array_iter (deck, &context.core->decks) {
        U64 idx = str_search(needle, deck->path);
        if (idx != ARRAY_NIL_IDX) {
            if (deck->active) {
                array_insert(&view->search_results, deck, 0);
            } else {
                array_push(&view->search_results, deck);
            }
        }
    }

    array_iter (deck, &view->search_results) {
        GtkWidget *card = deck_view_card_new(view, deck);
        gtk_box_append(GTK_BOX(view->decks->box), card);
    }
}

static Void deck_view_new () {
    tmem_new(tm);

    if (context.view) ui_view_destroy(context.view);
    Auto view = ui_view_new<ViewDecks>(4*KB, context.pub.roots.box, true, false);
    context.view = &view->base;
    defer { deck_view_do_search(); };

    //
    // search entry
    //
    view->entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(view->entry), tr("Search decks..."));
    gtk_box_append(GTK_BOX(view->base.box_left), view->entry);

    g_idle_add([](Void *data){
        Auto view = reinterpret_cast<ViewDecks*>(data);
        gtk_widget_grab_focus(view->entry);
        return 0;
    }, view);

    //
    // import options
    //
    GtkWidget *group = ui_row_group_new();
    gtk_box_append(GTK_BOX(view->base.box_left), group);

    //
    // import decks
    //
    GtkWidget *b = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, SPACING);

    view->import_paths = ui_file_picker_new(context.view->arena, tr("Import multiple decks"), true, false, {});
    gtk_box_append(GTK_BOX(b), view->import_paths->widget);
    gtk_widget_set_hexpand(view->import_paths->entry, true);
    gtk_widget_set_halign(view->import_paths->entry, GTK_ALIGN_FILL);

    UiPopover *import_help = ui_popover_new(context.view->arena, "kronomi-question-symbolic", 0, false, false, false);
    gtk_box_append(GTK_BOX(b), import_help->widget);
    ui_popover_set_info(import_help, context.view->arena,  fs_read_entire_file(tm, str("data/docs/flashcards_deck"), 0));

    UiRow *row = ui_row_new(context.view->arena, {}, b);
    gtk_box_append(GTK_BOX(group), row->widget);

    //
    // export deck
    //
    b = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, SPACING);

    view->export_path = ui_file_picker_new(context.view->arena, tr("Export decks"), false, true, {});
    gtk_box_append(GTK_BOX(b), view->export_path->widget);
    gtk_widget_set_hexpand(view->export_path->entry, true);
    gtk_widget_set_halign(view->export_path->entry, GTK_ALIGN_FILL);

    UiPopover *export_help = ui_popover_new(context.view->arena, "kronomi-question-symbolic", 0, false, false, false);
    gtk_box_append(GTK_BOX(b), export_help->widget);
    ui_popover_set_info(export_help, context.view->arena, fs_read_entire_file(tm, str("data/docs/flashcards_csv"), 0));

    row = ui_row_new(context.view->arena, {}, b);
    gtk_box_append(GTK_BOX(group), row->widget);

    //
    // delete row
    //
    group = ui_row_group_new();
    gtk_box_append(GTK_BOX(view->base.box_left), group);

    view->delete_checkbox = gtk_check_button_new();
    row = ui_row_new(context.view->arena, str(tr("Delete selected decks")), view->delete_checkbox);
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
    g_signal_connect(GTK_EDITABLE(view->entry), "changed", G_CALLBACK(+[](GtkEditable*, Void *){ deck_view_do_search(); }), 0);
    g_signal_connect(close_button->widget, "clicked", G_CALLBACK(+[](GtkWidget *, Void *){
        fc_load_deck(context.core, 0);
        main_view_new();
    }), 0);
    g_signal_connect(apply_button->confirm, "clicked", G_CALLBACK(+[](GtkWidget *, Void *){
        tmem_new(tm);

        Auto view = reinterpret_cast<ViewDecks*>(context.view);

        String import_paths = str(gtk_editable_get_text(GTK_EDITABLE(view->import_paths->entry)));
        String export_path  = str(gtk_editable_get_text(GTK_EDITABLE(view->export_path->entry)));
        Bool   delete_decks = ui_checkbox_checked(view->delete_checkbox);

        array_iter (deck, &view->search_results) fc_export_to_csv(context.core, deck, export_path);
        if (delete_decks) array_iter (deck, &view->search_results) fc_del_deck(context.core, deck);

        Auto paths = array_new<String>(tm);
        str_split(import_paths, str("|"), false, false, &paths);
        array_iter (path, &paths) fc_add_deck(context.core, false, path);

        flush_settings();
        deck_view_do_search();
    }), 0);
}

// =============================================================================
// Search View
// =============================================================================
struct CardSearchResult {
    I64 score;
    FlashCard *card;
};

struct ViewSearch {
    View base;

    UiLazyScrollbox *cards;
    GtkWidget *entry;
    UiIntPicker *bucket_restriction;
    GtkWidget *delete_checkbox;
    UiIntPicker *move_to_bucket;
    GtkWidget *do_fuzzy_search;

    Array<CardSearchResult> search_results;
    U64 next_card_idx;
};

static void search_view_do_search () {
    Auto view = reinterpret_cast<ViewSearch*>(context.view);

    if (view->cards) gtk_widget_unparent(view->cards->widget->widget);

    view->next_card_idx = 0;
    array_init(&view->search_results, context.view->arena);

    String needle = str(gtk_editable_get_text(GTK_EDITABLE(view->entry)));
    I64 bucket    = view->bucket_restriction->val;

    array_iter (card, &context.core->cards) {
        if ((bucket != -1) && (card->bucket != static_cast<U64>(bucket))) {
            continue;
        } else if (ui_checkbox_checked(view->do_fuzzy_search)) {
            I64 q = str_fuzzy_search(needle, card->question, 0);
            I64 a = str_fuzzy_search(needle, card->answer, 0);
            I64 score = max(q, a);
            if (score != INT64_MIN) array_push_lit(&view->search_results, .score=score, .card=card);
        } else {
            U64 q = str_search(needle, card->question);
            U64 a = str_search(needle, card->answer);
            if (q != ARRAY_NIL_IDX || a != ARRAY_NIL_IDX) array_push_lit(&view->search_results, .score=0, .card=card);
        }
    }

    if (ui_checkbox_checked(view->do_fuzzy_search)) {
        array_sort_cmp(&view->search_results, [](Auto a, Auto b){ return (a->score < b->score) ? 1 : 0; });
    }

    view->cards = ui_lazy_scrollbox_new(context.view->arena, true, [](Void *data){
        Auto view = reinterpret_cast<ViewSearch*>(data);
        Auto c = array_try_ref(&view->search_results, view->next_card_idx++);
        return c ? ui_flashcard_new(context.view->arena, c->card)->widget->widget : 0;
    }, view);

    gtk_box_append(GTK_BOX(view->base.box_right), view->cards->widget->widget);
    gtk_widget_set_hexpand(view->cards->widget->box, true);
    gtk_widget_set_vexpand(view->cards->widget->box, true);
    gtk_widget_set_halign(view->cards->widget->box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(view->cards->widget->box, GTK_ALIGN_START);
}

static Void search_view_new () {
    if (context.view) ui_view_destroy(context.view);
    Auto view = ui_view_new<ViewSearch>(4*KB, context.pub.roots.box, true, false);
    context.view = &view->base;
    defer { search_view_do_search(); };

    //
    // search entry and options
    //
    GtkWidget *group = ui_row_group_new();
    gtk_box_append(GTK_BOX(view->base.box_left), group);

    view->entry = gtk_entry_new();
    UiRow *row = ui_row_new(context.view->arena, {}, view->entry);
    gtk_entry_set_placeholder_text(GTK_ENTRY(view->entry), tr("Search cards..."));
    gtk_box_append(GTK_BOX(group), row->widget);
    gtk_widget_set_hexpand(view->entry, true);
    gtk_widget_set_halign(view->entry, GTK_ALIGN_FILL);

    view->do_fuzzy_search = gtk_check_button_new();
    row = ui_row_new(context.view->arena, str(tr("Do fuzzy search")), view->do_fuzzy_search);
    gtk_box_append(GTK_BOX(group), row->widget);

    view->bucket_restriction = ui_int_picker_new(context.view->arena, -1, 5, -1);
    row = ui_row_new(context.view->arena, str(tr("Search in bucket (-1 for all buckets)")), view->bucket_restriction->widget);
    gtk_box_append(GTK_BOX(group), row->widget);

    g_idle_add([](Void *data){
        Auto view = reinterpret_cast<ViewSearch*>(data);
        gtk_widget_grab_focus(view->entry);
        return 0;
    }, view);

    //
    // bulk edit options
    //
    group = ui_row_group_new();
    gtk_box_append(GTK_BOX(view->base.box_left), group);

    view->move_to_bucket = ui_int_picker_new(context.view->arena, -1, 5, -1);
    row = ui_row_new(context.view->arena, str(tr("Move to bucket (-1 for no move)")), view->move_to_bucket->widget);
    gtk_box_append(GTK_BOX(group), row->widget);

    view->delete_checkbox = gtk_check_button_new();
    row = ui_row_new(context.view->arena, str(tr("Delete selected items")), view->delete_checkbox);
    gtk_box_append(GTK_BOX(group), row->widget);
    gtk_widget_add_css_class(row->label, "kronomi-error-color");

    //
    // buttons
    //
    GtkWidget *button_group = ui_button_group_new(false, true);
    gtk_box_append(GTK_BOX(view->base.box_left), button_group);

    UiButton *close_button = ui_button_new(context.view->arena, 0, tr("Close"), true, false, false);
    gtk_box_append(GTK_BOX(button_group), close_button->widget);

    UiPopoverConfirm *ok_button = ui_popover_confirm_new(context.view->arena, 0, tr("Apply"), true, false, false);
    gtk_box_append(GTK_BOX(button_group), ok_button->widget->widget);

    //
    // listen
    //
    g_signal_connect(GTK_EDITABLE(view->entry), "changed", G_CALLBACK(+[](GtkEditable*, Void *){ search_view_do_search(); }), 0);
    g_signal_connect(view->do_fuzzy_search, "toggled", G_CALLBACK(+[](GtkCheckButton*, Void *){ search_view_do_search(); }), 0);
    g_signal_connect(view->bucket_restriction->entry, "changed", G_CALLBACK(+[](GtkWidget*, Void *){ search_view_do_search(); }), 0);
    g_signal_connect(close_button->widget, "clicked", G_CALLBACK(+[](GtkWidget *, Void *){
        fc_flush_deck(context.core);
        main_view_new();
    }), 0);
    g_signal_connect(ok_button->confirm, "clicked", G_CALLBACK(+[](GtkWidget *, Void *data){
        Auto view = reinterpret_cast<ViewSearch*>(data);

        if (ui_checkbox_checked(view->delete_checkbox)) {
            array_iter (x, &view->search_results) fc_del_card(context.core, x.card);
        } else {
            I64 move_to_bucket = view->move_to_bucket->val;
            if (move_to_bucket != -1) array_iter (x, &view->search_results) x.card->bucket = move_to_bucket;
        }

        search_view_do_search();
    }), view);
}

// =============================================================================
// Editor View
// =============================================================================
struct ViewEditor {
    View base;
    UiMarkupEditor2 *question_editor;
    UiMarkupEditor2 *answer_editor;
    UiIntPicker *bucket_picker;
    FlashCard *card;
};

static Void editor_view_new (FlashCard *card) {
    if (context.view) ui_view_destroy(context.view);
    Auto view = ui_view_new<ViewEditor>(4*KB, context.pub.roots.box, true, true);
    context.view = &view->base;
    view->card = card;

    //
    // editors
    //
    view->question_editor = ui_markup_editor2_new(context.view->arena, card ? card->question : str(""), tr("Question"), tr("Question Preview"));
    gtk_box_append(GTK_BOX(view->base.box_left), view->question_editor->entry_card->widget);
    gtk_box_append(GTK_BOX(view->base.box_right), view->question_editor->markup_card->widget);

    g_idle_add([](Void *data){
        Auto view = reinterpret_cast<ViewEditor*>(data);
        gtk_widget_grab_focus(view->question_editor->ed->text_view->entry);
        return 0;
    }, view);

    view->answer_editor = ui_markup_editor2_new(context.view->arena, card ? card->answer : str(""), tr("Answer"), tr("Answer Preview"));
    gtk_box_append(GTK_BOX(view->base.box_left), view->answer_editor->entry_card->widget);
    gtk_box_append(GTK_BOX(view->base.box_right), view->answer_editor->markup_card->widget);

    //
    // bucket picker
    //
    view->bucket_picker = ui_int_picker_new(context.view->arena, 0, 5, card ? card->bucket : 0);
    UiRow *row = ui_row_new(context.view->arena, str(tr("Bucket")), view->bucket_picker->widget);
    gtk_box_append(GTK_BOX(view->base.box_left), row->widget);

    //
    // buttons
    //
    GtkWidget *button_group = ui_button_group_new(false, true);
    gtk_box_append(GTK_BOX(view->base.box_left), button_group);

    UiButton *cancel_button = ui_button_new(context.view->arena, 0, tr("Cancel"), true, false, false);
    gtk_box_append(GTK_BOX(button_group), cancel_button->widget);

    UiButton *ok_button = ui_button_new(context.view->arena, 0, tr("Ok"), true, false, false);
    gtk_box_append(GTK_BOX(button_group), ok_button->widget);

    //
    // listen
    //
    g_signal_connect(cancel_button->widget, "clicked", G_CALLBACK(+[](GtkWidget *, Void *){ main_view_new(); }), 0);
    g_signal_connect(ok_button->widget, "clicked", G_CALLBACK(+[](GtkWidget *, Void *data){
        Auto view = reinterpret_cast<ViewEditor*>(data);
        if (view->card) fc_del_card(context.core, view->card);
        I64 bucket = view->bucket_picker->val;
        tmem_new(tm);
        String question = ui_text_view_get_text(tm, view->question_editor->ed->text_view);
        String answer = ui_text_view_get_text(tm, view->answer_editor->ed->text_view);
        fc_add_card(context.core, bucket, question, answer);
        fc_flush_deck(context.core);
        main_view_new();
    }), view);
}

// =============================================================================
// Main view
// =============================================================================
struct ViewMain {
    View base;
};

static Void main_view_new () {
    tmem_new(tm);

    if (context.view) ui_view_destroy(context.view);
    Auto view = ui_view_new<ViewMain>(4*KB, context.pub.roots.box, false, false);
    context.view = &view->base;

    if (context.core->decks.count == 0) {
        deck_view_new();
        return;
    }

    if (context.core->fragmentation > 100) fc_load_deck(context.core, 0);

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

    UiButton *exam_button = ui_button_new(context.view->arena, "kronomi-exam-symbolic", 0, false, false, false);
    gtk_box_append(GTK_BOX(floating_buttons), exam_button->widget);

    UiPopover *help_button = ui_popover_new(context.view->arena, "kronomi-question-symbolic", 0, false, false, false);
    gtk_box_append(GTK_BOX(floating_buttons), help_button->widget);
    ui_popover_set_info(help_button, context.view->arena, fs_read_entire_file(tm, str("data/docs/flashcards"), 0));
    gtk_popover_set_position(GTK_POPOVER(help_button->popover), GTK_POS_RIGHT);

    //
    // cards
    //
    U64 *gen_data = mem_new(context.view->arena, U64);
    UiLazyScrollbox *lazy = ui_lazy_scrollbox_new(context.view->arena, true, [](Void *data){
        U64 *idx = reinterpret_cast<U64*>(data);
        FlashCard *card = array_try_get(&context.core->cards, *idx);

        if (card) {
            *idx += 1;
            Auto w = ui_flashcard_new(context.view->arena, card);
            return w->widget->widget;
        } else {
            return reinterpret_cast<GtkWidget*>(0);
        }
    }, gen_data);

    gtk_box_append(GTK_BOX(view->base.box_right), lazy->widget->widget);
    gtk_widget_set_halign(lazy->widget->box, GTK_ALIGN_CENTER);

    //
    // listen
    //
    g_signal_connect(add_button->widget, "clicked", G_CALLBACK(+[](GtkWidget *, Void *){ editor_view_new(0); }), 0);
    g_signal_connect(search_button->widget, "clicked", G_CALLBACK(+[](GtkWidget *, Void *){ search_view_new(); }), 0);
    g_signal_connect(deck_button->widget, "clicked", G_CALLBACK(+[](GtkWidget *, Void *){ deck_view_new(); }), 0);
    g_signal_connect(exam_button->widget, "clicked", G_CALLBACK(+[](GtkWidget *, Void *){ exam_view_new(); }), 0);
}

// =============================================================================
// Applet
// =============================================================================
static Void flush_settings () {
    tmem_new(tm);

    AString a = astr_new(tm);
    astr_push_cstr(&a, "decks = [");
    array_iter (deck, &context.core->decks) {
        astr_push_cstr(&a, "    {\n");
        astr_push_fmt(&a, "         active = %s\n", deck->active ? "true" : "false");
        astr_push_fmt(&a, "         path   = \"%.*s\"\n", STR(deck->path));
        astr_push_cstr(&a, "    }\n");
    }
    astr_push_cstr(&a, "]");

    fs_write_entire_file(context.settings_path, astr_to_str(&a));
}

static Void load_settings () {
    tmem_new(tm);

    AString log = astr_new(tm);
    ConfigParser *p = config_load(tm, context.settings_path, &log);

    Bool error = false;
    ConfigAst *val = config_get_array(p, p->root, "decks", &error);
    if (! error) {
        array_iter (deck, &val->children) {
            Bool error  = false;
            Bool active = config_get_bool(p, deck, "active", &error);
            String path = config_get_string(p, deck, "path", &error);
            if (! error) fc_add_deck(context.core, active, path);
        }
    }

    astr_println(&log);
}

Applet *ui_flashcards_init (AppletRoots *roots) {
    context.pub.roots = *roots;
    context.pub.name = str("Flashcards");
    context.core = fc_new(&mem_root);

    context.settings_path = astr_fmt(&mem_root, "%.*s/flashcards.txt", STR(main_context.config_dir_path));
    if (! fs_file_exists(context.settings_path)) fs_write_entire_file(context.settings_path, str("decks = []"));

    load_settings();
    main_view_new();
    return &context.pub;
}
