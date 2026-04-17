#pragma once

#include "base/core.h"
#include "base/log.h"
#include "base/mem.h"
#include "base/math.h"
#include "lexer/lexer.h"

// X(tag, type)
#define EACH_CONFIG_AST_NODE(X)\
    X(CONFIG_AST_ROOT, ConfigAstRoot)\
    X(CONFIG_AST_ERROR, ConfigAstError)\
    X(CONFIG_AST_BOOL, ConfigAstBool)\
    X(CONFIG_AST_ARRAY, ConfigAstArray)\
    X(CONFIG_AST_STRING, ConfigAstString)\
    X(CONFIG_AST_STRUCT, ConfigAstStruct)\
    X(CONFIG_AST_I64, ConfigAstI64)\
    X(CONFIG_AST_U64, ConfigAstU64)\
    X(CONFIG_AST_F64, ConfigAstF64)\
    X(CONFIG_AST_KEYVAL, ConfigAstKeyval)

ienum (ConfigAstTag, U8) {
    #define X(TAG, TYPE, ...) TAG, e##TYPE=TAG,
        EACH_CONFIG_AST_NODE(X)
    #undef X
    CONFIG_AST_TAG_COUNT
};

istruct (ConfigAst) {
    ConfigAstTag tag;
    SrcPos pos;
    Array(ConfigAst*) children;
};

istruct (ConfigAstRoot)   { ConfigAst base; };
istruct (ConfigAstError)  { ConfigAst base; };
istruct (ConfigAstBool)   { ConfigAst base; Bool value; };
istruct (ConfigAstArray)  { ConfigAst base; };
istruct (ConfigAstString) { ConfigAst base; String value; };
istruct (ConfigAstStruct) { ConfigAst base; };
istruct (ConfigAstI64)    { ConfigAst base; I64 value; };
istruct (ConfigAstU64)    { ConfigAst base; U64 value; };
istruct (ConfigAstF64)    { ConfigAst base; F64 value; };
istruct (ConfigAstKeyval) { ConfigAst base; String key; };

istruct (Config) {
    Mem *mem;
    Lexer *lex;
    AString log;
    String text;
    String path;
    ConfigAst *root;
    SrcPos last_eaten_token;
};

extern CString config_ast_tag_to_cstr    [CONFIG_AST_TAG_COUNT];
extern U64     config_ast_get_node_size  [CONFIG_AST_TAG_COUNT];
extern U8      config_ast_get_node_align [CONFIG_AST_TAG_COUNT];

Config    *config_parse      (Mem *, String);
Bool       config_get_bool   (Config *, ConfigAst *, CString);
I64        config_get_i64    (Config *, ConfigAst *, CString);
U64        config_get_u64    (Config *, ConfigAst *, CString);
F64        config_get_f64    (Config *, ConfigAst *, CString);
Vec2       config_get_vec2   (Config *, ConfigAst *, CString);
Vec4       config_get_vec4   (Config *, ConfigAst *, CString);
String     config_get_string (Config *, ConfigAst *, CString, Mem *);
ConfigAst *config_get_array  (Config *, ConfigAst *, CString);
ConfigAst *config_get_struct (Config *, ConfigAst *, CString);
