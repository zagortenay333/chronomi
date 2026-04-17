#pragma once

#include "base/core.h"
#include "base/mem.h"
#include "base/string.h"

istruct (Buf);

istruct (BufLineIter) {
    Buf *buf;
    U8 delimiter;
    U8 delimiter_len;
    U64 idx;
    U64 offset;
    String text;
    Bool done;
};

Buf         *buf_new               (Mem *, String);
Buf         *buf_new_from_file     (Mem *, String filepath);
BufLineIter *buf_line_iter_new     (Buf *, Mem *, U8 delimiter);
Bool         buf_line_iter_next    (BufLineIter *);
Void         buf_clear             (Buf *);
Void         buf_insert            (Buf *, U64 offset, String str);
Void         buf_delete            (Buf *, U64 offset, U64 count);
U64          buf_get_version       (Buf *buf);
U64          buf_get_count         (Buf *);
String       buf_get_str           (Buf *, Mem *);
String       buf_get_slice         (Buf *, Mem *, U64 offset, U64 count);
Bool         buf_ends_with_newline (Buf *);
U64          buf_find_prev_word    (Buf *, U64 from);
U64          buf_find_next_word    (Buf *, U64 from);

#define buf_iter_lines(IT, BUF, MEM)\
    for (BufLineIter *IT = buf_line_iter_new(BUF, MEM, 0); !IT->done; buf_line_iter_next(IT))

#define buf_iter_lines_delim(IT, BUF, MEM, DELIM)\
    for (BufLineIter *IT = buf_line_iter_new(BUF, MEM, DELIM); !IT->done; buf_line_iter_next(IT))
