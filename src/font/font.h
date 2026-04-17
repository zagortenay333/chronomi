#pragma once

#include <hb.h>
#include <hb-ft.h>
#include "base/core.h"
#include "base/map.h"
#include "window/window.h"

istruct (Font);

istruct (GlyphInfo) {
    U32 x;
    U32 y;
    U32 x_advance;
    U32 y_advance;
    U32 codepoint;
    U32 glyph_index;
    U32 byte_offset;
    Font *font;
};

istruct (AtlasSlot) {
    U16 x;
    U16 y;
    U32 width;
    U32 height;
    I32 bearing_x;
    I32 bearing_y;
    I32 advance;
    FT_Pixel_Mode pixel_mode;
    U32 glyph_index;
    AtlasSlot *lru_next;
    AtlasSlot *lru_prev;
};

istruct (FontCache);

istruct (Font) {
    Mem *mem;
    FontCache *cache;

    U64 last_touched;
    String filepath;
    String binary;
    FT_Face ft_face;
    hb_face_t *hb_face;
    hb_font_t *hb_font;

    U32 size; // As given to font_get().
    U32 height;
    U32 width;
    U32 ascent;
    U32 descent;

    AtlasSlot lru;
    Array(AtlasSlot*) free_slots;
    Map(U32, AtlasSlot*) slot_map;

    Texture atlas_texture;
    U16 atlas_slot_size;
};

typedef Void (*VertexFlushFn)();

istruct (FontCache) {
    Mem *mem;
    Array(Font*) fonts;
    FT_Library ft_lib;
    VertexFlushFn vertex_flush_fn;
    U16 atlas_size;
    U64 gc;
};

array_typedef(GlyphInfo, GlyphInfo);

// IMPORTANT: Fonts get automatically freed after not getting
// touched (read font_get() called) for about an hour. So do
// not store pointers to fonts on long lived data structures.
// Instead, store font paths, and use font_get to temporarily
// obtain a pointer.
FontCache     *font_cache_new       (Mem *, VertexFlushFn, U16 atlas_size);
Font          *font_get             (FontCache *, String filepath, U32 size);
AtlasSlot     *font_get_atlas_slot  (Font *, GlyphInfo *);
SliceGlyphInfo font_get_glyph_infos (Font *, Mem *, String);
