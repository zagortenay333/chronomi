#pragma once

#include "util/core.h"
#include "util/array.h"
#include "util/string.h"

struct FlashCard {
    U64 bucket;
    String question;
    String answer;
};

struct FlashDeck {
    Bool active;
    String path;
};

// For now all memory for the loaded deck sits in the deck_arena.
// Deleting tasks is a nop which leads to fragmentation. In order
// to defragment the arena, call todo fc_load_deck(). We track
// the number of deleted/updated tasks with the fragmentation var.
//
// @todo Maybe we should put all memory into an arena instead of
// just the memory related to the loaded deck
struct Flashcards {
    Mem *mem;
    Arena *deck_arena;
    U64 fragmentation;
    U64 session;
    FlashDeck *loaded_deck;
    Array<FlashCard*> cards;
    Array<FlashDeck*> decks;
};

Flashcards *fc_new              (Mem *);
Void        fc_mark_active_deck (Flashcards *, FlashDeck *);
Void        fc_load_deck        (Flashcards *, FlashDeck *);
Void        fc_flush_deck       (Flashcards *);
FlashDeck  *fc_add_deck         (Flashcards *, Bool active, String path);
Void        fc_del_deck         (Flashcards *, FlashDeck *);
FlashCard  *fc_add_card         (Flashcards *, U64, String, String);
Void        fc_del_card         (Flashcards *, FlashCard *);
Void        fc_export_to_csv    (Flashcards *, FlashDeck *, String);
