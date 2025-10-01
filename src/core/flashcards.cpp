#include "core/flashcards.h"
#include "core/config.h"
#include "os/fs.h"

Flashcards *fc_new (Mem *mem) {
    Auto fc = mem_new(mem, Flashcards);
    fc->mem = mem;
    fc->deck_arena = arena_new(fc->mem, 4*KB);
    array_init(&fc->decks, mem);
    return fc;
}

Void fc_mark_active_deck (Flashcards *fc, FlashDeck *deck) {
    array_iter (d, &fc->decks) {
        if (d == deck) d->active = true;
        else           d->active = false;
    }
}

Void fc_flush_deck (Flashcards *fc) {
    if (! fc->loaded_deck) return;

    tmem_new(tm);

    AString a = astr_new(tm);
    astr_push_fmt(&a, "session = %lu\ncards = [\n", fc->session);
    array_iter (card, &fc->cards) {
        String question = str_escape(tm, card->question);
        String answer = str_escape(tm, card->answer);
        astr_push_fmt(&a, "    {\n");
        astr_push_fmt(&a, "        bucket   = %lu\n", card->bucket);
        astr_push_fmt(&a, "        question = %.*s\n", STR(question));
        astr_push_fmt(&a, "        answer   = %.*s\n", STR(answer));
        astr_push_fmt(&a, "    }\n");
    }
    astr_push_cstr(&a, "]\n");

    fs_write_entire_file(fc->loaded_deck->path, astr_to_str(&a));
}

Void fc_load_deck (Flashcards *fc, FlashDeck *deck) {
    arena_pop_all(fc->deck_arena);
    array_init(&fc->cards, &fc->deck_arena->base);
    fc->fragmentation = 0;

    if (! deck) deck = array_find_get(&fc->decks, [](Auto it){ return it->active; });
    if (! deck) { deck = array_try_get(&fc->decks, 0); if (deck) deck->active = true; }
    if (! deck) return;

    fc->loaded_deck = deck;

    tmem_new(tm);

    AString log = astr_new(tm);
    ConfigParser *p = config_load(tm, deck->path, &log);

    Bool error = false;
    fc->session = config_get_u64(p, p->root, "session", &error);
    ConfigAst *cards = config_get_array(p, p->root, "cards", &error);
    if (! error) {
        array_iter (card, &cards->children) {
            Bool error      = false;
            U64 bucket      = config_get_u64(p, card, "bucket", &error);
            String question = config_get_string(p, card, "question", &error);
            String answer   = config_get_string(p, card, "answer", &error);
            if (! error) fc_add_card(fc, bucket, question, answer);
        }
    }

    astr_println(&log);
}

FlashDeck *fc_add_deck (Flashcards *fc, Bool active, String path) {
    Auto deck = mem_new(fc->mem, FlashDeck);
    deck->active = active;
    deck->path = str_copy(fc->mem, path);
    array_push(&fc->decks, deck);
    return deck;
}

Void fc_del_deck (Flashcards *fc, FlashDeck *deck) {
    array_find_remove(&fc->decks, [&](FlashDeck *it){ return it == deck; });
    array_maybe_decrease_capacity(&fc->decks);
    if (deck == fc->loaded_deck) fc_load_deck(fc, 0);
    mem_free(fc->mem, .old_ptr=deck->path.data, .old_size=deck->path.count);
    mem_free(fc->mem, .old_ptr=deck, .old_size=sizeof(deck));
}

FlashCard *fc_add_card (Flashcards *fc, U64 bucket, String question, String answer) {
    Auto card = mem_new(&fc->deck_arena->base, FlashCard);
    card->bucket = bucket;
    card->question = str_copy(&fc->deck_arena->base, question);
    card->answer = str_copy(&fc->deck_arena->base, answer);
    array_push(&fc->cards, card);
    return card;
}

Void fc_del_card (Flashcards *fc, FlashCard *card) {
    fc->fragmentation++;
    array_find_remove(&fc->cards, [&](FlashCard *it){ return it == card; });
}

// @todo It's weird that we have to load a deck and
// unload the previous one just to export to csv...
Void fc_export_to_csv (Flashcards *fc, FlashDeck *deck, String export_dir_path) {
    tmem_new(tm);

    FlashDeck *prev = fc->loaded_deck;
    defer { fc_load_deck(fc, prev); };

    String name = str_suffix_from_last(deck->path, '/');
    name = str_prefix_to_last(name, '.');
    String export_path = astr_fmt(tm, "%.*s/%.*s.csv", STR(export_dir_path), STR(name));

    AString csv = astr_new(tm);
    fc_load_deck(fc, deck);
    array_iter (card, &fc->cards) {
        astr_push_byte(&csv, '"');
        astr_push_str(&csv, str_replace_all(tm, card->question, str("\""), str("\"\"")));
        astr_push_cstr(&csv, "\",\"");
        astr_push_str(&csv, str_replace_all(tm, card->answer, str("\""), str("\"\"")));
        astr_push_cstr(&csv, "\"\n");
    }
    fs_write_entire_file(export_path, astr_to_str(&csv));
}
