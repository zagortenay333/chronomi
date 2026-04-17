#include "map.h"

#define MAX_LOAD     70u
#define MIN_LOAD     20u
#define MIN_CAPACITY 16u
#define hashof(E)    (*cast(UMapHash*, E))

assert_static(MAP_HASH_OF_EMPTY_ENTRY == 0);
assert_static((MAX_LOAD < 100) && (MIN_LOAD < 100));

// Performs quadratic probing via triangular numbers.
// Pass cmp = 0 to only look for empty slots.
inl UMapEntry *probe (UMap *map, UMapCmp cmp, UMapKey *key, UMapHash hash) {
    assert_dbg(is_pow2(map->capacity));
    assert_dbg(hash >= MAP_HASH_OF_FILLED_ENTRY);

    U8 *entries = map->entries;
    U64 koffset = map->schema.key_offset;
    U64 esize   = map->schema.entry_size;
    U64 mask    = map->capacity - 1;
    U64 idx     = hash & mask;
    U64 inc     = 1;

    while (true) {
        UMapEntry *entry = &entries[idx * esize];
        if (hashof(entry) == MAP_HASH_OF_EMPTY_ENTRY) return entry;
        if (cmp && (hashof(entry) == hash) && cmp(key, cast(U8*, entry) + koffset)) return entry;
        idx  = (idx + inc) & mask;
        inc += 1;
    }
}

static Void rehash (UMap *map, U64 new_cap) {
    U64 esize       = map->schema.entry_size;
    UMap old_map    = *map;
    map->tomb_count = 0;
    map->capacity   = new_cap;
    map->entries    = mem_alloc(map->mem, U8, .zeroed=true, .size=(new_cap * esize));
    umap_iter (o, UMapEntry, &old_map) memcpy(probe(map, 0, 0, hashof(o)), o, esize);
    mem_free(map->mem, .old_ptr=old_map.entries, .old_size=(old_map.capacity * esize));
}

static Void maybe_grow (UMap *map) {
    U64 max_load = safe_mul(map->capacity, MAX_LOAD) / 100;
    if ((map->count + map->tomb_count) > max_load) {
        rehash(map, (map->count > max_load) ? safe_mul(2u, map->capacity) : map->capacity);
    }
}

static Void maybe_shrink (UMap *map) {
    if (map->capacity <= MIN_CAPACITY) return;
    U64 min_load = safe_mul(map->capacity, MIN_LOAD) / 100;
    if (map->count < min_load) rehash(map, map->capacity / 2);
}

Void umap_clear (UMap *map) {
    memset(map->entries, 0, map->capacity * map->schema.entry_size);
    map->tomb_count = 0;
    map->count = 0;
}

UMapEntry *umap_get (UMap *map, UMapKey *key) {
    UMapHash hash = max(map->schema.hasher(key), MAP_HASH_OF_FILLED_ENTRY);
    UMapEntry *entry = probe(map, map->schema.cmp, key, hash);
    return (hashof(entry) < MAP_HASH_OF_FILLED_ENTRY) ? 0 : entry;
}

// Caller sets values on returned entry.
// If a new entry was created, out_found will be set to true.
UMapEntry *umap_add (UMap *map, UMapKey *key, Bool *out_found) {
    maybe_grow(map);
    UMapHash hash = max(map->schema.hasher(key), MAP_HASH_OF_FILLED_ENTRY);
    UMapEntry *entry = probe(map, map->schema.cmp, key, hash);
    Bool found = (hashof(entry) >= MAP_HASH_OF_FILLED_ENTRY);
    if (out_found) *out_found = found;
    if (! found) {
        map->count++;
        hashof(entry) = hash; 
        memcpy(cast(U8*, entry) + map->schema.key_offset, key, map->schema.key_size);
    }
    return entry;
}

Bool umap_remove (UMap *map, UMapKey *key) {
    UMapHash hash = max(map->schema.hasher(key), MAP_HASH_OF_FILLED_ENTRY);
    UMapEntry *entry = probe(map, map->schema.cmp, key, hash);
    Bool found = (hashof(entry) >= MAP_HASH_OF_FILLED_ENTRY);
    if (found) {
        map->count--;
        map->tomb_count++;
        hashof(entry) = MAP_HASH_OF_TOMB_ENTRY;
        if (map->shrink_on_del) maybe_shrink(map);
    }
    return found;
}

Void umap_init (UMap *map, Mem *mem, U64 cap, UMapSchema schema) {
    cap = max(MIN_CAPACITY, next_pow2(safe_mul(cap / MAX_LOAD, 100)));
    map->mem        = mem;
    map->count      = 0;
    map->capacity   = cap;
    map->tomb_count = 0;
    map->entries    = mem_alloc(mem, U8, .zeroed=true, .size=(cap * schema.entry_size));
    map->schema     = schema;
}
