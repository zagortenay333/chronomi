#include <errno.h>
#define XXH_STATIC_LINKING_ONLY
#include "vendor/xxhash/xxhash.h"
#include "base/string.h"
#include "os/fs.h"

// =============================================================================
// String:
// =============================================================================
Bool    is_word_char    (Char c)               { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c == '_') || (cast(U8, c) > 127); }
Bool    is_whitespace   (Char c)               { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
Bool    is_special_char (Char c)               { return !is_word_char(c) && !is_whitespace(c); }
CString cstr            (Mem *mem, String s)   { return astr_fmt(mem, "%.*s%c", STR(s), 0).data; }
String  str             (CString s)            { return (String){ .data=s, .count=cast(U64, strlen(s)) }; }
U64     str_hash_seed   (String str, U64 seed) { return XXH3_64bits_withSeed(str.data, str.count, seed); }
U64     str_hash        (String str)           { return str_hash_seed(str, 5381); }
Bool    str_match       (String s1, String s2) { return (s1.count == s2.count) && (!s1.count || (! strncmp(s1.data, s2.data, s1.count))); }
U64     istr_hash       (IString *i)           { return str_hash(*i); }
U64     cstr_hash       (CString s)            { return str_hash(str(s)); }
Bool    cstr_match      (CString a, CString b) { return str_match(str(a), str(b)); }
Void    str_clear       (String s, U8 b)       { memset(s.data, b, s.count); }

Bool str_starts_with (String str, String prefix) {
    if (str.count < prefix.count) return false;
    str.count = prefix.count;
    return str_match(str, prefix);
}

Bool str_ends_with (String str, String suffix) {
    if (str.count < suffix.count) return false;
    str = str_suffix_from(str, str.count - suffix.count);
    return str_match(str, suffix);
}

// Returns ARRAY_NIL_IDX if not found.
U64 str_index_of_first (String str, U8 byte) {
    return array_find(&str, IT == byte);
}

// Returns ARRAY_NIL_IDX if not found.
U64 str_index_of_last (String str, U8 byte) {
    array_iter_back (c, &str) if (c == byte) return ARRAY_IDX;
    return ARRAY_NIL_IDX;
}

String str_slice (String str, U64 offset, U64 count) {
    offset = min(offset, str.count);
    count  = min(count, str.count - offset);
    return (String){ .data=(str.data + offset), .count=count };
}

// Gets rid of whitespace at the beggining and end.
String str_trim (String str) {
    U64 start = 0;
    U64 end   = 0;
    array_iter (c, &str)      if (! is_whitespace(c)) { start = ARRAY_IDX; break; }
    array_iter_back (c, &str) if (! is_whitespace(c)) { end = ARRAY_IDX + 1; break; }
    return str_slice(str, start, end - start);
}

String str_cut_prefix (String str, String prefix) {
    if (str_starts_with(str, prefix)) {
        str.data  += prefix.count;
        str.count -= prefix.count;
    }
    return str;
}

String str_cut_suffix (String str, String suffix) {
    if (str_ends_with(str, suffix)) str.count -= suffix.count;
    return str;
}

// Non-inclusive.
String str_prefix_to (String str, U64 to_idx) {
    str.count = min(to_idx, str.count);
    return str;
}

// Inclusive.
String str_suffix_from (String str, U64 from_idx) {
    U64 idx    = min(from_idx, str.count);
    str.data  += idx;
    str.count -= idx;
    return str;
}

// Non-inclusive.
String str_prefix_to_first (String str, U8 byte) {
    array_iter (c, &str) if (c == byte) return str_prefix_to(str, ARRAY_IDX);
    return (String){};
}

// Non-inclusive.
String str_prefix_to_last (String str, U8 byte) {
    array_iter_back (c, &str) if (c == byte) return str_prefix_to(str, ARRAY_IDX);
    return (String){};
}

// Non-inclusive.
String str_suffix_from_last (String str, U8 byte) {
    array_iter_back (c, &str) if (c == byte) return str_suffix_from(str, ARRAY_IDX + 1);
    return (String){};
}

// Non-inclusive.
String str_suffix_from_first (String str, U8 byte) {
    array_iter (c, &str) if (c == byte) return str_suffix_from(str, ARRAY_IDX + 1);
    return (String){};
}

Bool str_to_i64 (CString str, I64 *out, U64 base) {
    errno = 0;
    Char *endptr = 0;
    *out = cast(U64, strtol(str, &endptr, base));
    return (errno == 0) && (endptr != str);
}

Bool str_to_u64 (CString str, U64 *out, U64 base) {
    errno = 0;
    Char *endptr = 0;
    *out = cast(U64, strtoul(str, &endptr, base));
    return (errno == 0) && (endptr != str);
}

Bool str_to_f64 (CString str, F64 *out) {
    errno = 0;
    Char *endptr = 0;
    *out = strtod(str, &endptr);
    return (errno == 0) && (endptr != str);
}

String str_copy (Mem *mem, String str) {
    if (! str.count) return (String){};
    Auto p = mem_alloc(mem, Char, .size=str.count);
    memcpy(p, str.data, str.count);
    return (String){.data=p, .count=str.count};
}

String str_replace_all (Mem *mem, String text, String needle, String replacement) {
    if (needle.count == 0) return text;

    AString a = astr_new(mem);
    String chunk = { .data=text.data, .count=0 };

    array_iter (c, &text) {
        c;
        String suffix = str_suffix_from(text, ARRAY_IDX);

        if (str_starts_with(suffix, needle)) {
            astr_push_str(&a, chunk);
            astr_push_str(&a, replacement);
            chunk.data = chunk.data + chunk.count + needle.count;
            chunk.count = 0;
            ARRAY_IDX += needle.count - 1;
        } else {
            chunk.count++;
        }
    }

    astr_push_str(&a, chunk);
    return astr_to_str(&a);
}

String str_escape (Mem *mem, String string) {
    AString result = astr_new_cap(mem, max(string.count, 2lu));
    astr_push_u8(&result, '"');

    String chunk = { .data=string.data, .count=0 };

    array_iter (c, &string) {
        switch (c) {
        case '\n':
            astr_push_str(&result, chunk);
            astr_push_2u8(&result, '\\', 'n');
            chunk.data += chunk.count + 1;
            chunk.count = 0;
            break;
        case '"':
            astr_push_str(&result, chunk);
            astr_push_2u8(&result, '\\', '"');
            chunk.data += chunk.count + 1;
            chunk.count = 0;
            break;
        default:
            chunk.count++;
            break;
        }
    }

    astr_push_str(&result, chunk);
    astr_push_u8(&result, '"');
    return astr_to_str(&result);
}

String str_unescape (Mem *mem, String string) {
    AString result = astr_new_cap(mem, string.count);

    // Eliminate surrounding double quotes.
    if (array_get(&string, 0) == '"') {
        assert_always(string.count >= 2);
        assert_always(array_get_last(&string) == '"');
        string.data  += 1;
        string.count -= 2;
    }

    String chunk = { .data=string.data, .count=0 };

    array_iter (c, &string) {
        switch (c) {
        case '\\':
            astr_push_str(&result, chunk);
            chunk.data += chunk.count + 2;
            chunk.count = 0;
            ARRAY_IDX++;
            if (ARRAY_IDX == string.count) break;
            c = array_get(&string, ARRAY_IDX);
            switch (c) {
            case 'n': astr_push_byte(&result, '\n'); break;
            case 'r': astr_push_byte(&result, '\r'); break;
            case '"': astr_push_byte(&result, '"'); break;
            default:  astr_push_byte(&result, c); break;
            }
            break;
        default:
           chunk.count++;
           break;
        }
    }

    astr_push_str(&result, chunk);
    return astr_to_str(&result);
}

// This function splits the given 'str' into tokens separated by
// bytes that appear in the 'separators' string. For example, for
// the inputs:
//
//     str = "/a/b|c//foobar/"
//     separators = "/|"
//
// ... there are 4 possible outputs depending on the values of the
// 'keep_separators' and 'keep_empties' arguments:
//
//     1. [a] [b] [c] [foobar]
//     2. [] [a] [b] [c] [] [foobar] []
//     3. [/] [a] [/] [b] [|] [c] [/] [/] [foobar] [/]
//     4. [] [/] [a] [/] [b] [|] [c] [/] [] [/] [foobar] [/] []
//
Void str_split (String str, String separators, Bool keep_separators, Bool keep_empties, ArrayString *out) {
    U64 prev_pos = 0;

    array_iter (c, &str) {
        if (! array_has(&separators, c)) continue;
        if (keep_empties || (ARRAY_IDX > prev_pos)) array_push(out, str_slice(str, prev_pos, ARRAY_IDX - prev_pos));
        if (keep_separators) array_push(out, str_slice(str, ARRAY_IDX, 1));
        prev_pos = ARRAY_IDX + 1;
    }

    if (keep_empties || (str.count > prev_pos)) array_push(out, str_slice(str, prev_pos, str.count - prev_pos));
}

// This function will return ARRAY_NIL_IDX if the needle is not found.
U64 str_search (String needle, String haystack) {
    if (needle.count == 0) return 0;

    array_iter (c, &haystack) {
        c;
        String suffix = str_suffix_from(haystack, ARRAY_IDX);
        if (str_starts_with(suffix, needle)) return ARRAY_IDX;
    }

    return ARRAY_NIL_IDX;
}

// This functions searches the haystack for the needle in a fuzzy way.
// If the needle is *not* found it returns INT64_MIN; otherwise, the
// returned val indicates how close of a match it is (higher is better).
//
// If the 'tokens' argument is not NULL, this function will emit into it
// slices into the haystack where the matches were found. The last token
// emitted is special: it is the remainder of the haystack (from the end
// of the last match to the end of the haystack). This is useful if you
// wish to call this function on the remainder of the haystack over and
// over again until you exhaust it.
//
// The algorithm is a fairly simple O(n) search. First we look ahead to
// see if all chars in the needle appear in the haystack in the exact
// order and separated any number of chars. We then search in reverse
// in hopes of finding a shorter match:
//
//     a b c d e abcdef
//     -------------->|
//               |<----
//
// This algorithm does not try to find the optimal match:
//
//     a b c d e ab c def abcdef
//     ---------------->|
//               |<------
//
// The score is computed based on how many consecutive letters in the
// text were found, whether letters appear at word beginnings, number
// of gaps between letters, ...
I64 str_fuzzy_search (String needle, String haystack, ArrayString *tokens) {
    if (needle.count == 0) return 0;
    if (needle.count > haystack.count) return INT64_MIN;

    U64 needle_cursor = 0;
    U64 haystack_end  = 0;

    { // 1. Search forwards to find the initial match:
        array_iter (b, &haystack) {
            if (b == needle.data[needle_cursor]) {
                needle_cursor++;
                if (needle_cursor == needle.count) { haystack_end = ARRAY_IDX; break; }
            }
        }

        if (needle_cursor != needle.count) return INT64_MIN;
        needle_cursor--;
    }

    tmem_new(tm);
    ArrayU64 indices; // Map from needle idx to haystack idx.
    if (tokens) { array_init(&indices, tm); array_ensure_count(&indices, needle.count, 0); }

    I64 gaps            = 0;
    I64 consecutives    = 0;
    I64 word_beginnings = 0;

    { // 2. Compute score while searching again in reverse:
        U64 prev_match_idx = ARRAY_NIL_IDX;

        array_iter_back_from (b, &haystack, haystack_end) {
            if (b != needle.data[needle_cursor]) {
                gaps++;
            } else {
                if (tokens) array_set(&indices, needle_cursor, ARRAY_IDX);
                if ((ARRAY_IDX + 1) == prev_match_idx) consecutives++;
                if ((ARRAY_IDX > 1) && is_whitespace(haystack.data[ARRAY_IDX - 1])) word_beginnings++;
                if (needle_cursor == 0) break;
                needle_cursor--;
                prev_match_idx = ARRAY_IDX;
            }
        }

        assert_dbg(needle_cursor == 0);
    }

    if (tokens) { // 3. Emit tokens:
        String token = str_slice(haystack, indices.data[0], 1);

        array_iter_from (i, &indices, 1) {
            if (i == indices.data[ARRAY_IDX - 1] + 1) {
                token.count++;
            } else {
                array_push(tokens, token);
                token = str_slice(haystack, i, 1);
            }
        }

        array_push(tokens, token);
        array_push(tokens, str_slice(haystack, array_get_last(&indices) + 1, haystack.count));
    }

    return max(INT64_MIN+1, (consecutives * 4) + (word_beginnings * 3) - gaps);
}

U64 str_codepoint_count (String s) {
    U64 count = 0;
    str_utf8_iter (it, s) count++;
    return count;
}

static U8 utf8_class [32] = {
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,2,2,2,2,3,3,4,5,
};

UtfDecode str_utf8_decode (String str) {
    UtfDecode result = {1, UINT32_MAX};

    U8 byte = array_get(&str, 0);
    U8 byte_class = utf8_class[byte >> 3];

    switch (byte_class) {
    case 1: {
        result.codepoint = byte;
        result.inc = 1;
    } break;

    case 2: {
        if (str.count >= 2) {
            U8 b = str.data[1];
            if (utf8_class[b >> 3] == 0) {
                result.codepoint  = (byte & 0b11111) << 6;
                result.codepoint |= (b & 0b111111);
                result.inc = 2;
            }
        }
    } break;

    case 3: {
        if (str.count >= 3) {
            U8 b[2] = {str.data[1], str.data[2]};
            if (utf8_class[b[0] >> 3] == 0 &&
                utf8_class[b[1] >> 3] == 0
            ) {
                result.codepoint  = (byte & 0b1111) << 12;
                result.codepoint |= ((b[0] & 0b111111) << 6);
                result.codepoint |= (b[1] & 0b111111);
                result.inc = 3;
            }
        }
    } break;

    case 4: {
        if (str.count >= 4) {
            U8 b[3] = {str.data[1], str.data[2], str.data[3]};
            if (utf8_class[b[0] >> 3] == 0 &&
                utf8_class[b[1] >> 3] == 0 &&
                utf8_class[b[2] >> 3] == 0
            ) {
                result.codepoint  = (byte & 0b111) << 18;
                result.codepoint |= ((b[0] & 0b111111) << 12);
                result.codepoint |= ((b[1] & 0b111111) << 6);
                result.codepoint |= (b[2]  & 0b111111);
                result.inc = 4;
            }
        }
    }
    }

    return result;
}

UtfIter str_utf8_iter_new (String str) {
    return (UtfIter){ .str=str };
}

Bool str_utf8_iter_next (UtfIter *it) {
    if (it->str.count == 0) return false;
    UtfDecode d = str_utf8_decode(it->str);
    it->decode = d;
    it->str.data += d.inc;
    it->str.count -= d.inc;
    return true;
}

String str_utf32_to_utf8 (Mem *mem, U32 codepoint) {
    String d = { .count=5, .data=mem_alloc(mem, Char, .size=5) };

    U32 mask2  = 0x00000003;
    U32 mask3  = 0x00000007;
    U32 mask4  = 0x0000000f;
    U32 mask5  = 0x0000001f;
    U32 mask6  = 0x0000003f;

    if (codepoint <= 0x7F) {
        d.data[0] = cast(U8, codepoint);
        d.count = 1;
    } else if (codepoint <= 0x7FF) {
        d.data[0] = (mask2 << 6) | ((codepoint >> 6) & mask5);
        d.data[1] = (1<<7) | (codepoint & mask6);
        d.count = 2;
    } else if (codepoint <= 0xFFFF) {
        d.data[0] = (mask3 << 5) | ((codepoint >> 12) & mask4);
        d.data[1] = (1<<7) | ((codepoint >> 6) & mask6);
        d.data[2] = (1<<7) | ( codepoint       & mask6);
        d.count = 3;
    } else if (codepoint <= 0x10FFFF) {
        d.data[0] = (mask4 << 4) | ((codepoint >> 18) & mask3);
        d.data[1] = (1<<7) | ((codepoint >> 12) & mask6);
        d.data[2] = (1<<7) | ((codepoint >>  6) & mask6);
        d.data[3] = (1<<7) | ( codepoint        & mask6);
        d.count = 4;
    } else {
        d.data[0] = '?';
        d.count = 1;
    }

    return d;
}

// =============================================================================
// AString:
// =============================================================================
Void    astr_print         (AString *a)                           { if (a->count) printf("%.*s", STR(*a)); }
Void    astr_println       (AString *a)                           { if (a->count) printf("%.*s\n", STR(*a)); }
CString astr_to_cstr       (AString *a)                           { astr_push_byte(a, 0); return a->data; }
String  astr_to_str        (AString *a)                           { return a->as_slice; }
Void    astr_push_u8       (AString *a, U8 v)                     { array_push_n(a, v); }
Void    astr_push_2u8      (AString *a, U8 x, U8 y)               { array_push_n(a, x, y); }
Void    astr_push_3u8      (AString *a, U8 x, U8 y, U8 z)         { array_push_n(a, x, y, z); }
Void    astr_push_u16      (AString *a, U16 v)                    { array_push_n(a, v>>0, v>>8); }
Void    astr_push_u32      (AString *a, U64 v)                    { array_push_n(a, v>>0, v>>8, v>>16, v>>24); }
Void    astr_push_u64      (AString *a, U64 v)                    { array_push_n(a, v>>0, v>>8, v>>16, v>>24, v>>32, v>>40, v>>48, v>>56); }
Void    astr_push_byte     (AString *a, U8 b)                     { array_push(a, b); }
Void    astr_push_bytes    (AString *a, U8 b, U64 n)              { SliceU8 s; array_increase_count_o(a, n, false, &s); if (n) memset(s.data, b, n); }
Void    astr_push_str      (AString *a, String s)                 { array_push_many(a, &s); }
Void    astr_push_cstr     (AString *a, CString s)                { astr_push_str(a, (String){ .data=s, .count=(U64)(strlen(s)) }); }
Void    astr_push_cstr_nul (AString *a, CString s)                { astr_push_str(a, (String){ .data=s, .count=(U64)(strlen(s) + 1) }); }
Void    astr_push_2cstr    (AString *a, CString s1, CString s2)   { astr_push_cstr(a, s1); astr_push_cstr(a, s2); }
Void    astr_push_fmt      Fmt(2, 3) (AString *a, CString f, ...) { astr_push_fmt_vam(a, f); }

Void astr_push_fmt_va Fmt(2, 0) (AString *astr, CString fmt, VaList va) {
    VaList va2;
    va_copy(va2, va);
    Int fmt_len = vsnprintf(0, 0, fmt, va);
    assert_always(fmt_len >= 0);
    array_ensure_capacity(astr, fmt_len + 1);
    vsnprintf(astr->data + astr->count, fmt_len + 1, fmt, va2);
    astr->count += fmt_len;
    va_end(va2);
}

// Append the str argument wrapped in double quotes with
// any double quotes within str escaped with a backslash:
//
//     (foo "bar" baz)  ->  ("foo \"bar\" baz")
//
Void astr_push_str_quoted (AString *astr, String str) {
    astr_push_byte(astr, '"');

    Bool escaped = false;
    String chunk = { .data = str.data };

    array_iter (c, &str) {
        if (escaped) {
            escaped = false;
            chunk.count++;
        } else if (c == '"') {
            astr_push_str(astr, chunk);
            astr_push_byte(astr, '\\');
            astr_push_byte(astr, '"');
            chunk.count = 0;
            chunk.data = array_try_ref(&str, ARRAY_IDX + 1);
        } else if (c == '\\') {
            escaped = true;
            chunk.count++;
        } else {
            chunk.count++;
        }
    }

    astr_push_str(astr, chunk);
    astr_push_byte(astr, '"');
}
