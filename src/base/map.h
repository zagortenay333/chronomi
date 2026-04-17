#pragma once

// =============================================================================
// Overview:
// ---------
//
// A polymorphic open addressing hash table in the form of a
// low-level untyped data structure (UMap) and a type-safe
// macro wrapper (Map) that covers most use cases.
//
// Data is stored inside UMapEntry's whose layout is mostly
// user defined. UMap only requires that each UMapEntry starts
// with a UMapHash.
//
// Input hashes are auto-adjusted by UMap in order to reserve
// some low values for metadata use.
//
// Usage example:
// --------------
//
//     istruct (Entry) {
//         UMapHash hash;
//         U64 key;
//         CString val;
//     };
//
//     Bool cmp (UMapKey *a, UMapKey *b) {
//         return *cast(U64*, a) == *cast(U64*, b);
//     }
//
//     UMapHash hasher (UMapKey *k) {
//         return *cast(U64*, k) + 1;
//     }
//
//     UMap m; umap_init(&m, tm, 512, (UMapSchema){
//         .entry_size = sizeof(Entry),
//         .key_offset = offsetof(Entry, key),
//         .key_size   = sizeof(U64),
//         .hasher     = hasher,
//         .cmp        = cmp,
//     });
//
//     cast(Entry*, umap_add(&m, &(U64){42}, 0))->val = "Hello";
//     cast(Entry*, umap_add(&m, &(U64){11}, 0))->val = "world!";
//     umap_iter (e, Entry, &m) printf("h=%u k=%u v=%s\n", e->hash, e->key, e->val);
//
// =============================================================================
#include "base/string.h"

typedef Void UMapEntry;
typedef U64  UMapHash;
typedef Void UMapKey;
typedef Bool (*UMapCmp) (UMapKey*, UMapKey*);
typedef UMapHash (*UMapHasher) (UMapKey*);

istruct (UMapSchema) {
    U16 entry_size;
    U16 key_offset;
    U16 key_size;
    UMapCmp cmp;
    UMapHasher hasher;
};

istruct (UMap) {
    Mem *mem;
    U64 count;
    U64 capacity;
    U64 tomb_count;
    U8 *entries;
    Bool shrink_on_del;
    UMapSchema schema;
};

#define MAP_HASH_OF_EMPTY_ENTRY  0u // Marks an entry as empty.
#define MAP_HASH_OF_TOMB_ENTRY   1u // Marks an entry as a tombstone (internal use).
#define MAP_HASH_OF_FILLED_ENTRY 2u // Values >= to this mark occupied entries.

#define umap_iter(IT, T, M)         let2(MAP, MAP_IDX, M, 0u) UMAP_ITER(IT, T, MAP->schema.entry_size, MAP)
#define umap_iter_from(IT, T, M, I) let2(MAP, MAP_IDX, M, I)  UMAP_ITER(IT, T, MAP->schema.entry_size, MAP)
#define UMAP_ITER(IT, T, N, MAP)    for (T *IT; (MAP_IDX < MAP->capacity) && (IT = cast(T*, &MAP->entries[MAP_IDX*N]), true); MAP_IDX++)\
                                    if (*cast(UMapHash*, IT) >= MAP_HASH_OF_FILLED_ENTRY)

Void       umap_init   (UMap *, Mem *, U64 cap, UMapSchema);
Void       umap_clear  (UMap *);
UMapEntry *umap_add    (UMap *, UMapKey *, Bool *out_found); // Caller sets value.
UMapEntry *umap_get    (UMap *, UMapKey *); // Returns 0 if not found.
Bool       umap_remove (UMap *, UMapKey *);

// =============================================================================
// Type-safe wrapper around UMap:
// ------------------------------
//
// With the exception of map_init, all the methods here work
// with any key and value types.
//
// The map_init macro hardcodes some key types. If you need
// custom key types, then use map_init_hc or use umap_init
// directly.
//
// Usage example:
// --------------
//
//     Map(U64, CString) map;
//     map_init(&map, mem_root);
//     map_add(&map, 42, "Hello world!");
//     map_add(&map, 420, "Foo bar baz!");
//     map_iter (e, &map) printf("hash=%u key=%u val=%s\n", e->hash, e->key, e->val);
//
// =============================================================================
#define Map(K, V) union {\
    UMap umap;\
    struct { UMapHash hash; K key; V val; } *E;\
    struct { V val; Bool found; } *R;\
}

#define MapEntry(M) Type(*(M)->E)
#define MapKey(M)   Type((M)->E->key)
#define MapVal(M)   Type((M)->E->val)
#define MapRes(M)   Type(*(M)->R)
#define Set(K)      Map(K, U8)

inl Bool map_cmp_u32  (UMapKey *a, UMapKey *b) { return *cast(U32*, a) == *cast(U32*, b); }
inl Bool map_cmp_u64  (UMapKey *a, UMapKey *b) { return *cast(U64*, a) == *cast(U64*, b); }
inl Bool map_cmp_istr (UMapKey *a, UMapKey *b) { return *cast(IString**, a) == *cast(IString**, b); }
inl Bool map_cmp_str  (UMapKey *a, UMapKey *b) { return str_match(*cast(String*, a), *cast(String*, b)); }
inl Bool map_cmp_cstr (UMapKey *a, UMapKey *b) { return cstr_match(*cast(CString*, a), *cast(CString*, b)); }

inl UMapHash map_hash_u32  (UMapKey *k) { return hash_u32(*cast(U32*, k)); }
inl UMapHash map_hash_u64  (UMapKey *k) { return hash_u64(*cast(U64*, k)); }
inl UMapHash map_hash_istr (UMapKey *k) { return istr_hash(*cast(IString**, k)); }
inl UMapHash map_hash_str  (UMapKey *k) { return str_hash(*cast(String*, k)); }
inl UMapHash map_hash_cstr (UMapKey *k) { return cstr_hash(*cast(CString*, k)); }

#define map_init_hc(M, MEM, C, H, CMP)\
    umap_init(&(M)->umap, mem_base(MEM), C, ((UMapSchema){\
        .entry_size  = sizeof(MapEntry(M)),\
        .key_size    = sizeof(MapKey(M)),\
        .key_offset  = offsetof(MapEntry(M), key),\
        .cmp         = CMP,\
        .hasher      = H,\
    }))

#define map_init_cap(M, MEM, C)\
    umap_init(&(M)->umap, mem_base(MEM), C, ((UMapSchema){\
        .entry_size  = sizeof(MapEntry(M)),\
        .key_size    = sizeof(MapKey(M)),\
        .key_offset  = offsetof(MapEntry(M), key),\
        .cmp         = typematch(MapKey(M), U32:map_cmp_u32,  U64:map_cmp_u64,  String:map_cmp_str,  CString:map_cmp_cstr,  IString*:map_cmp_istr),\
        .hasher      = typematch(MapKey(M), U32:map_hash_u32, U64:map_hash_u64, String:map_hash_str, CString:map_hash_cstr, IString*:map_hash_istr),\
    }))

#define map_init(M, MEM)     map_init_cap(M, MEM, 0)
#define map_clear(M)         umap_clear(&(M)->umap)
#define map_remove(M, K)     ({ def2(m, k, M, acast(MapKey(M), K)); umap_remove(&m->umap, &k); })
#define map_get(M, K, O)     ({ def2(m, k, M, acast(MapKey(M), K)); Auto _(E) = cast(MapEntry(m)*, umap_get(&m->umap, &k)); if (_(E)) {*(O) = _(E)->val;} !!_(E); })
#define map_has(M, K)        ({ def2(m, k, M, acast(MapKey(M), K)); Auto _(E) = cast(MapEntry(m)*, umap_get(&m->umap, &k)); !!_(E); })
#define map_get_ptr(M, K)    ({ def2(m, k, M, acast(MapKey(M), K)); Auto _(E) = cast(MapEntry(m)*, umap_get(&m->umap, &k)); _(E) ? _(E)->val : NULL; })
#define map_get_assert(M, K) ({ def2(m, k, M, acast(MapKey(M), K)); cast(MapEntry(m)*, umap_get(&m->umap, &k))->val; })
#define map_uadd(M, K, O)    ({ def2(m, k, M, acast(MapKey(M), K)); &cast(MapEntry(m)*, umap_add(&m->umap, &k, O))->val; })
#define map_add(M, K, V)     ({ def3(m, k, v, M, acast(MapKey(M), K), acast(MapVal(M), V)); cast(MapEntry(m)*, umap_add(&m->umap, &k, 0))->val = v; })

#define map_iter(IT, M)         let2(MAP, MAP_IDX, M, 0u) UMAP_ITER(IT, MapEntry(MAP), sizeof(MapEntry(MAP)), (&MAP->umap))
#define map_iter_from(IT, M, I) let2(MAP, MAP_IDX, M, I)  UMAP_ITER(IT, MapEntry(MAP), sizeof(MapEntry(MAP)), (&MAP->umap))

// Use this for removing an entry while looping over the table.
#define map_iter_remove(SLOT, MAP) ({\
    def2(slot, map, SLOT, MAP);\
    slot->hash = MAP_HASH_OF_TOMB_ENTRY;\
    map->umap.count--;\
    map->umap.tomb_count++;\
    MAP_IDX--;\
})
