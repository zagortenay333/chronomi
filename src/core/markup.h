#pragma once

#include "util/core.h"
#include "util/log.h"
#include "util/map.h"

// X(tag, type)
#define EACH_MARKUP_AST_NODE(X)\
    X(MARKUP_AST_ROOT, MarkupAstRoot)\
    X(MARKUP_AST_DUMMY, MarkupAstDummy)\
    X(MARKUP_AST_LIST, MarkupAstList)\
    X(MARKUP_AST_LIST_ITEM, MarkupAstListItem)\
    X(MARKUP_AST_ORDERED_LIST, MarkupAstOrderedList)\
    X(MARKUP_AST_SEPARATOR, MarkupAstSeparator)\
    X(MARKUP_AST_RAW_BLOCK, MarkupAstRawBlock)\
    X(MARKUP_AST_HEADER, MarkupAstHeader)\
    X(MARKUP_AST_PARAGRAPH, MarkupAstParagraph)\
    X(MARKUP_AST_TABLE, MarkupAstTable)\
    X(MARKUP_AST_TABLE_ROW, MarkupAstTableRow)\
    X(MARKUP_AST_TABLE_CELL, MarkupAstTableCell)\
    X(MARKUP_AST_META, MarkupAstMeta)\
    X(MARKUP_AST_TEXT, MarkupAstText)\
    X(MARKUP_AST_SUB, MarkupAstSub)\
    X(MARKUP_AST_SUP, MarkupAstSup)\
    X(MARKUP_AST_STRIKE, MarkupAstStrike)\
    X(MARKUP_AST_BOLD, MarkupAstBold)\
    X(MARKUP_AST_LINK, MarkupAstLink)\
    X(MARKUP_AST_ITALIC, MarkupAstItalic)\
    X(MARKUP_AST_HIGHLIGHT, MarkupAstHighlight)\
    X(MARKUP_AST_RAW_INLINE, MarkupAstRawInline)\
    X(MARKUP_AST_TAG_REF, MarkupAstTagRef)\
    X(MARKUP_AST_FILTER_NOT, MarkupAstFilterNot)\
    X(MARKUP_AST_FILTER_OR, MarkupAstFilterOr)\
    X(MARKUP_AST_FILTER_AND, MarkupAstFilterAnd)\
    X(MARKUP_AST_FILTER_ANY, MarkupAstFilterAny)\
    X(MARKUP_AST_FILTER_DUE, MarkupAstFilterDue)\
    X(MARKUP_AST_FILTER_DONE, MarkupAstFilterDone)\
    X(MARKUP_AST_FILTER_PIN, MarkupAstFilterPin)\
    X(MARKUP_AST_FILTER_HIDE, MarkupAstFilterHide)\
    X(MARKUP_AST_FILTER_TAG, MarkupAstFilterTag)\
    X(MARKUP_AST_FILTER_ERROR, MarkupAstFilterError)\
    X(MARKUP_AST_FILTER_TRACK, MarkupAstFilterTrack)\
    X(MARKUP_AST_FILTER_FUZZY, MarkupAstFilterFuzzy)\
    X(MARKUP_AST_FILTER_STRING, MarkupAstFilterString)\
    X(MARKUP_AST_FILTER_PRIORITY, MarkupAstFilterPriority)

struct MarkupAstTableCellConfig {
    I64 width;
    I64 height;
    Bool no_bg;
};

enum MarkupAstAdmonition: U8 {
    MARKUP_AST_ADMONITION_TIP,
    MARKUP_AST_ADMONITION_NOTE,
    MARKUP_AST_ADMONITION_WARNING,
    MARKUP_AST_ADMONITION_IMPORTANT,
};

fenum (MarkupAstMetaConfigFlags, U16) {
    MARKUP_AST_META_CONFIG_HAS_PRIORITY    = flag(0),
    MARKUP_AST_META_CONFIG_HAS_TRACK       = flag(1),
    MARKUP_AST_META_CONFIG_HAS_CREATED     = flag(2),
    MARKUP_AST_META_CONFIG_HAS_COMPLETED   = flag(3),
    MARKUP_AST_META_CONFIG_HAS_DUE         = flag(4),
    MARKUP_AST_META_CONFIG_HAS_PIN         = flag(5),
    MARKUP_AST_META_CONFIG_HAS_DONE        = flag(6),
    MARKUP_AST_META_CONFIG_HAS_HIDE        = flag(7),
    MARKUP_AST_META_CONFIG_HAS_SPOILER     = flag(8),
    MARKUP_AST_META_CONFIG_HAS_TAGS        = flag(9),
    MARKUP_AST_META_CONFIG_HAS_BODY_TAGS   = flag(10),
    MARKUP_AST_META_CONFIG_HAS_IMAGE_WIDTH = flag(11),
    MARKUP_AST_META_CONFIG_HAS_IMAGE_PATH  = flag(12),
    MARKUP_AST_META_CONFIG_HAS_ADMONITION  = flag(13),

    MARKUP_AST_META_TODO_FLAGS =
        MARKUP_AST_META_CONFIG_HAS_PRIORITY |
        MARKUP_AST_META_CONFIG_HAS_TRACK |
        MARKUP_AST_META_CONFIG_HAS_CREATED |
        MARKUP_AST_META_CONFIG_HAS_COMPLETED |
        MARKUP_AST_META_CONFIG_HAS_DUE |
        MARKUP_AST_META_CONFIG_HAS_PIN |
        MARKUP_AST_META_CONFIG_HAS_DONE |
        MARKUP_AST_META_CONFIG_HAS_HIDE |
        MARKUP_AST_META_CONFIG_HAS_TAGS
};

struct MarkupAstMetaConfig {
    MarkupAstMetaConfigFlags flags;
    MarkupAstAdmonition admonition;
    U32 image_width;
    String image_path;
    U64 priority;
    U64 track;
    String created;
    String completed;
    String due;
    String spoiler;
    Set<String> tags; // Tags that appear in the header.
    Set<String> body_tags; // Tags that appear in the body.
};

enum MarkupAstTag: U8 {
    #define X(TAG, TYPE, ...) TAG, e##TYPE=TAG,
        EACH_MARKUP_AST_NODE(X)
    #undef X

    MARKUP_AST_TAG_COUNT
};

struct MarkupAst {
    MarkupAstTag tag;
    SrcPos pos;
    Array<MarkupAst*> children;
};

struct MarkupAstRoot           { MarkupAst base; };
struct MarkupAstDummy          { MarkupAst base; };
struct MarkupAstList           { MarkupAst base; };
struct MarkupAstListItem       { MarkupAst base; };
struct MarkupAstOrderedList    { MarkupAst base; };
struct MarkupAstSeparator      { MarkupAst base; };
struct MarkupAstRawBlock       { MarkupAst base; };
struct MarkupAstHeader         { MarkupAst base; U64 size; };
struct MarkupAstParagraph      { MarkupAst base; };
struct MarkupAstTable          { MarkupAst base; };
struct MarkupAstTableRow       { MarkupAst base; };
struct MarkupAstTableCell      { MarkupAst base; MarkupAstTableCellConfig config; };
struct MarkupAstMeta           { MarkupAst base; MarkupAstMetaConfig *config; };
struct MarkupAstText           { MarkupAst base; };
struct MarkupAstSub            { MarkupAst base; };
struct MarkupAstSup            { MarkupAst base; };
struct MarkupAstStrike         { MarkupAst base; };
struct MarkupAstBold           { MarkupAst base; };
struct MarkupAstLink           { MarkupAst base; String link, alias; };
struct MarkupAstItalic         { MarkupAst base; };
struct MarkupAstHighlight      { MarkupAst base; };
struct MarkupAstRawInline      { MarkupAst base; Bool monospace; };
struct MarkupAstTagRef         { MarkupAst base; };
struct MarkupAstFilterNot      { MarkupAst base; };
struct MarkupAstFilterOr       { MarkupAst base; };
struct MarkupAstFilterAnd      { MarkupAst base; };
struct MarkupAstFilterAny      { MarkupAst base; };
struct MarkupAstFilterDue      { MarkupAst base; };
struct MarkupAstFilterDone     { MarkupAst base; };
struct MarkupAstFilterPin      { MarkupAst base; };
struct MarkupAstFilterHide     { MarkupAst base; };
struct MarkupAstFilterError    { MarkupAst base; };
struct MarkupAstFilterTag      { MarkupAst base; String text; };
struct MarkupAstFilterTrack    { MarkupAst base; U64 id; };
struct MarkupAstFilterFuzzy    { MarkupAst base; String needle; };
struct MarkupAstFilterString   { MarkupAst base; String needle; };
struct MarkupAstFilterPriority { MarkupAst base; U64 priority; };

extern CString markup_ast_tag_to_cstr    [MARKUP_AST_TAG_COUNT];
extern U64     markup_ast_get_node_size  [MARKUP_AST_TAG_COUNT];
extern U8      markup_ast_get_node_align [MARKUP_AST_TAG_COUNT];

extern MarkupAstTableCellConfig default_table_cell_config;

MarkupAst        *markup_ast_alloc        (Mem *, MarkupAstTag);
String            markup_ast_get_text     (MarkupAst *, String);
Slice<MarkupAst*> markup_ast_path_for_idx (Mem *, MarkupAst *, U64);
MarkupAst        *markup_parse            (Mem *, String);
MarkupAst        *markup_filter_parse     (Mem *, String);
U64               markup_ast_hash         (MarkupAst *);
