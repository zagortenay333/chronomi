#include "buffer/buffer.h"
#include "os/fs.h"
#include "os/time.h"

istruct (Buf) {
    Mem *mem;
    ArrayChar data;
    U64 version;
};

Buf *buf_new (Mem *mem, String text) {
    Auto buf = mem_new(mem, Buf);
    buf->mem = mem;
    array_init(&buf->data, mem);
    buf_insert(buf, 0, text);
    return buf;
}

Buf *buf_new_from_file (Mem *mem, String filepath) {
    Auto buf = mem_new(mem, Buf);
    buf->mem = mem;
    buf->version = 1;

    String file = fs_read_entire_file(mem, filepath, 1*KB);
    buf->data.mem = mem;
    buf->data.data = file.data;
    buf->data.count = file.count;
    buf->data.capacity = file.count + 1*KB + 1;

    return buf;
}

static String get_segment (BufLineIter *it, Char *p) {
    Char *end = it->buf->data.data + it->buf->data.count;

    if (p >= end) {
        it->delimiter_len = 0;
        return (String){p, 0};
    }

    if (it->delimiter == 0) {
        Char *cursor = p;
        while (cursor < end) {
            if (*cursor == '\n') {
                it->delimiter_len = 1;
                return (String){p, cursor - p};
            }

            if (*cursor == '\r') {
                if (cursor + 1 < end && cursor[1] == '\n') {
                    it->delimiter_len = 2;
                } else {
                    it->delimiter_len = 1;
                }

                return (String){p, cursor - p};
            }

            cursor++;
        }
    } else {
        Char *e = memchr(p, it->delimiter, end - p);
        if (e) {
            it->delimiter_len = 1;
            return (String){p, e - p};
        }
    }

    it->delimiter_len = 0;
    return (String){p, end - p};
}

BufLineIter *buf_line_iter_new (Buf *buf, Mem *mem, U8 delimiter) {
    Auto it = mem_new(mem, BufLineIter);
    it->delimiter = delimiter;
    it->buf = buf;
    if (buf->data.count) {
        it->idx = 0;
        it->text = get_segment(it, buf->data.data);
        it->offset = it->text.data - buf->data.data;
    } else {
        it->done = true;
    }
    return it;
}

Bool buf_line_iter_next (BufLineIter *it) {
    if (it->done) return true;
    it->idx++;
    it->text = get_segment(it, it->text.data + it->text.count + it->delimiter_len);
    it->offset = it->text.data - it->buf->data.data;
    if (it->text.data >= it->buf->data.data + it->buf->data.count) it->done = true;
    return it->done;
}

Void buf_insert (Buf *buf, U64 offset, String str) {
    array_insert_many(&buf->data, &str, offset);
    buf->version++;
}

Void buf_delete (Buf *buf, U64 offset, U64 count) {
    array_remove_many(&buf->data, offset, count);
    buf->version++;
}

U64 buf_get_count (Buf *buf) {
    return buf->data.count;
}

String buf_get_str (Buf *buf, Mem *mem) {
    String s = buf->data.as_slice;
    return str_copy(mem, s);
}

String buf_get_slice (Buf *buf, Mem *, U64 offset, U64 count) {
    return str_slice(buf->data.as_slice, offset, count);
}

Void buf_clear (Buf *buf) {
    buf->data.count = 0;
}

Bool buf_ends_with_newline (Buf *buf) {
    return str_ends_with(buf->data.as_slice, str("\n"));
}

U64 buf_get_version (Buf *buf) {
    return buf->version;
}

U64 buf_find_prev_word (Buf *buf, U64 from) {
    if (buf->data.count == 0) return 0;

    Char *start = buf->data.data;
    Char *p = &buf->data.data[from];

    if (p > start) p--;

    while (p > start) {
        if (is_whitespace(*p)) p--;
        else break;
    }

    if (is_word_char(*p)) {
        while (p > start) {
            if (is_word_char(*p)) p--;
            else break;
        }
        if (! is_word_char(*p)) p++;
    }

    return p - start;
}

U64 buf_find_next_word (Buf *buf, U64 from) {
    if (buf->data.count == 0) return 0;

    Char *end = &buf->data.data[buf->data.count];
    Char *p = &buf->data.data[from];

    while (p < end) {
        if (is_whitespace(*p)) p++;
        else break;
    }

    if (is_word_char(*p)) {
        while (p < end) {
            if (is_word_char(*p)) p++;
            else break;
        }
    } else if (p < end) {
        p++;
    }

    return p - buf->data.data;
}
