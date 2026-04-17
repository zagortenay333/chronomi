#pragma once

#include "base/core.h"
#include "base/array.h"
#include "ui/ui.h"
#include "ui/ui_view.h"

ienum (UiTileSplit, U8) {
    UI_TILE_SPLIT_NONE,
    UI_TILE_SPLIT_HORI,
    UI_TILE_SPLIT_VERT,
};

istruct (UiTileNode) {
    UiTileSplit split;

    F32 ratio;
    UiTileNode *parent;
    UiTileNode *child[2];

    U64 active_tab_idx;
    ArrayUiViewInstance tab_ids;
};

istruct (UiTileLeaf) {
    UiTileNode *node;
    U64 active_tab_id;
    UiBox *box;
};

array_typedef(UiTileLeaf, UiTileLeaf);

UiBox *ui_tile (String id, Mem *, UiTileNode **, UiViewStore *, Bool *);
