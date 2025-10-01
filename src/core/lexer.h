#pragma once

#include "util/log.h"
#include "util/core.h"
#include "util/string.h"

// X(tag value, TokenTag, stringified tag)
#define EACH_TOKEN(X)\
    X(0,    TOKEN_EOF, "EOF")\
    X('!',  TOKEN_EXCLAMATION, "!")\
    X('"',  TOKEN_DOUBLE_QUOTE, "\"")\
    X('#',  TOKEN_HASH, "#")\
    X('$',  TOKEN_DOLLAR, "$")\
    X('%',  TOKEN_PERCENT, "%")\
    X('&',  TOKEN_AMPERSAND, "&")\
    X('\'', TOKEN_SINGLE_QUOTE, "'")\
    X('(',  TOKEN_OPEN_PAREN, "(")\
    X(')',  TOKEN_CLOSED_PAREN, ")")\
    X('*',  TOKEN_ASTERISK, "*")\
    X('+',  TOKEN_PLUS, "+")\
    X(',',  TOKEN_COMMA, ",")\
    X('-',  TOKEN_MINUS, "-")\
    X('.',  TOKEN_DOT, ".")\
    X('/',  TOKEN_SLASH, "/")\
    X(':',  TOKEN_COLON, ":")\
    X(';',  TOKEN_SEMICOLON, ";")\
    X('<',  TOKEN_LESS, "<")\
    X('=',  TOKEN_EQUAL, "=")\
    X('>',  TOKEN_GREATER, ">")\
    X('?',  TOKEN_QUESTION_MARK, "?")\
    X('@',  TOKEN_AT, "@")\
    X('[',  TOKEN_OPEN_BRACKET, "[")\
    X('\\', TOKEN_BACKSLASH, "\\")\
    X(']',  TOKEN_CLOSED_BRACKET, "]")\
    X('^',  TOKEN_CARET, "^")\
    X('`',  TOKEN_BACKTICK, "`")\
    X('{',  TOKEN_OPEN_BRACE, "{")\
    X('|',  TOKEN_VBAR, "|")\
    X('}',  TOKEN_CLOSED_BRACE, "}")\
    X('~',  TOKEN_TILDE, "~")\
    X(' ',  TOKEN_SPACES, "spaces")\
    X('\n', TOKEN_NEWLINE, "\\n")\
    X('\t', TOKEN_TAB, "\\t")\
    X(256,  TOKEN_WORD, "word")\
    X(257,  TOKEN_U64, "U64")\
    X(258,  TOKEN_F64, "F64")\

enum TokenTag: I64 {
    #define X(VAL, TAG, ...) TAG = VAL,
        EACH_TOKEN(X)
    #undef X

    TOKEN_TAG_COUNT
};

struct Token {
    TokenTag tag;
    SrcPos pos;
    String text;
    U64 u64;
    F64 f64;
    Bool valid_number; // Whether a TOKEN_INT or TOKEN_FLOAT is valid.
};

struct Lexer;

// Instead of a giant array of tokens, we maintain
// a small ring buffer of size MAX_TOKEN_LOOKAHEAD.
// This means that all token structs eventually get
// overwritten, so don't hold them for long or copy.
const U64 MAX_TOKEN_LOOKAHEAD = 8;

Lexer *lex_new                (Mem *, Bool);
Void   lex_reset              (Lexer *, String);
Token *lex_peek               (Lexer *);
Token *lex_peek_nth           (Lexer *, U64 nth); // 1-indexed
Token *lex_try_peek           (Lexer *, TokenTag);
Token *lex_try_peek_nth       (Lexer *, U64 nth, TokenTag); // 1-indexed
Token *lex_eat                (Lexer *);
Token *lex_try_eat            (Lexer *, TokenTag);
Void   lex_eat_line           (Lexer *);
Void   lex_eat_whitespace     (Lexer *);
U64    lex_get_token_repeats  (Lexer *, Token *);
Void   lex_set_on_token_eaten (Lexer *, Void(*)(Token*, Void*), Void*);
