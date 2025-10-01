#pragma once

#include "util/core.h"
#include "util/log.h"
#include "util/mem.h"
#include "core/lexer.h"

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

enum ConfigAstTag: U8 {
    #define X(TAG, TYPE, ...) TAG, e##TYPE=TAG,
        EACH_CONFIG_AST_NODE(X)
    #undef X

    CONFIG_AST_TAG_COUNT
};

struct ConfigAst {
    ConfigAstTag tag;
    SrcPos pos;
    Array<ConfigAst*> children;
};

struct ConfigAstRoot    { ConfigAst base; };
struct ConfigAstError   { ConfigAst base; };
struct ConfigAstBool    { ConfigAst base; Bool value; };
struct ConfigAstArray   { ConfigAst base; };
struct ConfigAstString  { ConfigAst base; String value; };
struct ConfigAstStruct  { ConfigAst base; };
struct ConfigAstI64     { ConfigAst base; I64 value; };
struct ConfigAstU64     { ConfigAst base; U64 value; };
struct ConfigAstF64     { ConfigAst base; F64 value; };
struct ConfigAstKeyval  { ConfigAst base; String key; };

struct ConfigParser {
    Mem *mem;
    Lexer *lex;
    AString *log;
    String text;
    String path;
    ConfigAst *root;
    SrcPos last_eaten_token;
};

extern CString config_ast_tag_to_cstr    [CONFIG_AST_TAG_COUNT];
extern U64     config_ast_get_node_size  [CONFIG_AST_TAG_COUNT];
extern U8      config_ast_get_node_align [CONFIG_AST_TAG_COUNT];

ConfigParser *config_load       (Mem *, String, AString *);
Bool          config_get_bool   (ConfigParser *, ConfigAst *, CString, Bool *);
I64           config_get_i64    (ConfigParser *, ConfigAst *, CString, Bool *);
U64           config_get_u64    (ConfigParser *, ConfigAst *, CString, Bool *);
F64           config_get_f64    (ConfigParser *, ConfigAst *, CString, Bool *);
String        config_get_string (ConfigParser *, ConfigAst *, CString, Bool *);
ConfigAst    *config_get_array  (ConfigParser *, ConfigAst *, CString, Bool *);
ConfigAst    *config_get_struct (ConfigParser *, ConfigAst *, CString, Bool *);
