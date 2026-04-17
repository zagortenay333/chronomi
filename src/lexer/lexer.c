#include "base/mem.h"
#include "base/log.h"
#include "lexer/lexer.h"

istruct (Lexer) {
    Mem *mem;
    String text;

    U64 line;
    Char *eof;
    Char *cursor;

    Bool ignore_whitespace;
    Void (*on_token_eaten)(Token*, Void*);
    Void *user_data;

    U64 ring_count;
    U64 ring_cursor;
    Token ring[MAX_TOKEN_LOOKAHEAD];

    AString scratch;
};

#define lex_is_dec_digit(c) (c >= '0' && c <= '9')
#define lex_is_alpha(c)     ((c == '_') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || cast(I32, c) > 127)

// This is 0-indexed.
static I8 peek_nth_char (Lexer *lex, U64 lookahead) {
    U64 left_in_buf = (U64)(lex->eof - lex->cursor);
    return (lookahead < left_in_buf) ? lex->cursor[lookahead] : -1;
}

static I8 peek_char (Lexer *lex) {
    return (lex->cursor < lex->eof) ? *lex->cursor : 0;
}

static I8 eat_char (Lexer *lex) {
    if (lex->cursor == lex->eof) return 0;
    I8 result = *lex->cursor++;
    if (result == '\n') lex->line++;
    return result;
}

static Void build_word_token (Lexer *lex, Token *token) {
    token->tag = TOKEN_WORD;

    while (true) {
        U64 c = peek_char(lex);
        if (!lex_is_alpha(c) && !lex_is_dec_digit(c)) break;
        eat_char(lex);
    }

    token->text.count = lex->cursor - token->text.data;
}

static Void build_spaces_token (Lexer *lex, Token *token) {
    while (true) {
        U64 c = peek_char(lex);
        if (c != ' ') break;
        eat_char(lex);
    }

    token->text.count = lex->cursor - token->text.data;
}

static Void eat_whitespace (Lexer *lex) {
    while (true) {
        I8 c = peek_char(lex);

        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            eat_char(lex);
        } else {
            break;
        }
    }
}

static Void eat_digits (Lexer *lex) {
    while (true) {
        I8 c = peek_char(lex);
        if (c < '0' || c > '9') break;
        astr_push_byte(&lex->scratch, eat_char(lex));
    }
}

static Void build_number_token (Lexer *lex, Token *token) {
    lex->cursor--;
    lex->scratch.count = 0;

    eat_digits(lex);

    if (peek_char(lex) == '.' && lex_is_dec_digit(peek_nth_char(lex, 1))) {
        token->tag = TOKEN_F64;
        astr_push_byte(&lex->scratch, eat_char(lex));
        eat_digits(lex);
        astr_push_byte(&lex->scratch, 0);
        token->valid_number = str_to_f64(lex->scratch.data, &token->f64);
        if (! token->valid_number) token->f64 = 0;
    } else {
        token->tag = TOKEN_U64;
        astr_push_byte(&lex->scratch, 0);
        token->valid_number = str_to_u64(lex->scratch.data, &token->u64, 10);
        if (! token->valid_number) token->u64 = 0;
    }

    token->text.count = lex->cursor - token->text.data;
}

static Void build_token (Lexer *lex) {
    if (lex->ignore_whitespace) eat_whitespace(lex);

    U64 idx               = (lex->ring_cursor + lex->ring_count++) & (MAX_TOKEN_LOOKAHEAD - 1);
    Token *token          = &lex->ring[idx];
    token->text.data      = lex->cursor;
    token->text.count     = 1;
    token->tag            = cast(TokenTag, eat_char(lex));
    token->pos.offset     = token->text.data - lex->text.data;
    token->pos.first_line = lex->line;

    if (token->tag == 0) {
        token->text.count = 0;
    } else if (token->tag == ' ') {
        build_spaces_token(lex, token);
    } else if (lex_is_dec_digit(token->tag)) {
        build_number_token(lex, token);
    } else if (lex_is_alpha(token->tag)) {
        build_word_token(lex, token);
    }

    token->pos.last_line = lex->line;
    token->pos.length = token->text.count;
}

Lexer *lex_new (Mem *mem, Bool ignore_whitespace) {
    assert_dbg(MAX_TOKEN_LOOKAHEAD > 1);
    assert_dbg(is_pow2(MAX_TOKEN_LOOKAHEAD));
    Lexer *lex   = mem_new(mem, Lexer);
    lex->mem     = mem;
    lex->scratch = astr_new(mem);
    lex->ignore_whitespace = ignore_whitespace;
    return lex;
}

Void lex_reset (Lexer *lex, String text) {
    lex->text                = text;
    lex->scratch.count       = 0;
    lex->line                = 1;
    lex->ring_count          = 0;
    lex->ring_cursor         = 0;
    lex->cursor              = text.data;
    lex->eof                 = text.data + text.count;
}

Token *lex_peek_nth (Lexer *lex, U64 n) {
    assert_dbg(n > 0 && n <= MAX_TOKEN_LOOKAHEAD);
    while (lex->ring_count < n) build_token(lex);
    U64 idx = (lex->ring_cursor + n - 1) & (MAX_TOKEN_LOOKAHEAD - 1);
    return &lex->ring[idx];
}

Token *lex_peek (Lexer *lex) {
    return lex_peek_nth(lex, 1);
}

Token *lex_try_peek (Lexer *lex, TokenTag tag) {
    Token *token = lex_peek(lex);
    return (token->tag == tag) ? token : 0;
}

Token *lex_try_peek_nth (Lexer *lex, U64 nth, TokenTag tag) {
    Token *token = lex_peek_nth(lex, nth);
    return (token->tag == tag) ? token : 0;
}

Token *lex_eat (Lexer *lex) {
    Token *token     = lex_peek(lex);
    lex->ring_count -= 1;
    lex->ring_cursor = (lex->ring_cursor + 1) & (MAX_TOKEN_LOOKAHEAD - 1);
    if (lex->on_token_eaten) lex->on_token_eaten(token, lex->user_data);
    return token;
}

Token *lex_try_eat (Lexer *lex, TokenTag tag) {
    return (lex_peek(lex)->tag == tag) ? lex_eat(lex) : 0;
}

Void lex_eat_line (Lexer *lex) {
    Bool prev = lex->ignore_whitespace;
    lex->ignore_whitespace = false;

    while (true) {
        Token *token = lex_eat(lex);
        if (token->tag == '\n' || token->tag == TOKEN_EOF) break;
    }

    lex->ignore_whitespace = prev;
}

Void lex_eat_whitespace (Lexer *lex) {
    while (true) {
        Token *t = lex_peek(lex);
        if ((t->tag != TOKEN_SPACES) && (t->tag != '\n') && (t->tag != '\t')) break;
        lex_eat(lex);
    }
}

Void lex_set_on_token_eaten (Lexer *lex, Void(*fn)(Token*, Void*), Void *user_data) {
    lex->on_token_eaten = fn;
    lex->user_data = user_data;
}

// If the given token is made of one character, then
// return how many times this character is repeated
// consecutively right after the token. That is, for
// the string '***' calling this function on the first
// asterisk token returns 3.
U64 lex_get_token_repeats (Lexer *lex, Token *token) {
    if (token->tag == TOKEN_EOF || token->text.count != 1) return 1;
    Auto cursor = token->text.data + 1;
    Auto ch     = token->text.data[0];
    Auto end    = lex->text.data + lex->text.count;
    while (cursor < end && *cursor == ch) cursor++;
    return cursor - token->text.data;
}
