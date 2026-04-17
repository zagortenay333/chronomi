#include "array.h"

Void uarray_maybe_decrease_capacity (UArray *array, U64 esize) {
    if ((array->capacity > 4) && (array->count < (safe_mul(array->capacity, 25) / 100))) {
        U64 new_cap     = 2 * array->count;
        array->data     = mem_shrink(array->mem, U8, .size=(esize * new_cap), .old_ptr=array->data, .old_size=(esize * array->capacity));
        array->capacity = new_cap;
    }
}

Void uarray_increase_capacity (UArray *array, U64 esize, U64 n) {
    assert_dbg(n);
    U64 new_cap     = safe_add(array->capacity, n);
    array->data     = mem_grow(array->mem, U8, .size=(esize * new_cap), .old_ptr=array->data, .old_size=(esize * array->capacity));
    array->capacity = new_cap;
}

Void uarray_ensure_capacity (UArray *array, U64 esize, U64 n) {
    assert_dbg(n);
    U64 new_cap = array->capacity ?: n;
    while ((new_cap - array->count) < n) new_cap = safe_mul(new_cap, 2);
    U64 dt = new_cap - array->capacity;
    if (dt) uarray_increase_capacity(array, esize, dt);
}

Void uarray_ensure_capacity_min (UArray *array, U64 esize, U64 n) {
    U64 unused = array->capacity - array->count;
    if (unused < n) uarray_increase_capacity(array, esize, (n - unused));
}

Void uarray_increase_count (UArray *array, U64 esize, U64 n, Bool zeroed, USlice *out) {
    if (n) uarray_ensure_capacity(array, esize, n);
    USlice r = { .data=&array->data[esize * array->count], .count=n };
    array->count += n;
    if (zeroed) memset(r.data, 0, esize * n);
    if (out) *out = r;
}

Void *uarray_increase_count_p (UArray *array, U64 esize, U64 n, Bool zeroed) {
    USlice out = {};
    uarray_increase_count(array, esize, n, zeroed, &out);
    return out.data;
}

Void uarray_ensure_count (UArray *array, U64 esize, U64 n, Bool zeroed) {
    if (array->count < n) uarray_increase_count(array, esize, (n - array->count), zeroed, 0);
}

Void *uarray_push (UArray *array, U64 esize) {
    if (array->count == array->capacity) {
        U64 new_cap = array->capacity ? cast(U64, 1.8 * array->capacity) + 1 : 2;
        assert_always(new_cap > array->capacity);
        uarray_increase_capacity(array, esize, new_cap);
    }
    Void *r = &array->data[esize * array->count];
    array->count++;
    return r;
}

Void *uarray_insert (UArray *array, U64 esize, U64 idx) {
    if (idx == array->count) return uarray_push(array, esize);
    array_bounds_check(array, idx);
    uarray_ensure_capacity(array, esize, 1);
    U8 *p = &array->data[esize * idx];
    memmove(p + esize, p, esize * (array->count - idx));
    array->count++;
    return p;
}

Void uarray_remove (UArray *array, U64 esize, U64 idx) {
    array_bounds_check(array, idx);
    if (idx < (array->count - 1)) {
        U8 *src = &array->data[esize * (idx + 1)];
        U8 *dst = &array->data[esize * idx];
        memmove(dst, src, esize * (array->count - idx - 1));
    }
    array->count--;
}

Void uarray_remove_many (UArray *array, U64 esize, U64 idx, U64 n) {
    if (n == 0) return;
    array_bounds_check(array, idx + n - 1);
    if (idx < (array->count - 1)) {
        U8 *src = &array->data[esize * (idx + n)];
        U8 *dst = &array->data[esize * idx];
        memmove(dst, src, esize * (array->count - idx - n));
    }
    array->count -= n;
}

USlice uarray_insert_gap (UArray *array, U64 esize, U64 count, U64 idx, Bool zeroed) {
    idx = min(array->count, idx);
    U64 bytes_to_move = array->count - idx;
    uarray_increase_count(array, esize, count, false, 0);
    U8 *p = &array->data[esize * idx];
    memmove(&p[esize * count], p, esize * bytes_to_move);
    USlice r = { .data=p, .count=count };
    if (zeroed) memset(r.data, 0, esize * count);
    return r;
}

Void uarray_push_many (UArray *array, USlice *elems, U64 esize) {
    if (elems->count) {
        Void *p = uarray_increase_count_p(array, esize, elems->count, false);
        memcpy(p, elems->data, esize * elems->count);
    }
}

Void uarray_insert_many (UArray *array, USlice *elems, U64 esize, U64 idx) {
    if (elems->count) {
        U8 *p = uarray_insert_gap(array, esize, elems->count, idx, false).data;
        memcpy(p, elems->data, esize * elems->count);
    }
}

typedef Int(*Cmp)(const Void*, const Void*);

Void uarray_sort (UArray *array, U64 esize, Int(*cmp)(Void*, Void*)) {
    if (! array->data) return;
    qsort(array->data, array->count, esize, cast(Cmp, cmp));
}

U64 uarray_bsearch (UArray *array, U64 esize, Void *elem, Int(*cmp)(Void*, Void*)) {
    if (! array->data) return ARRAY_NIL_IDX;
    Auto p = bsearch(elem, array->data, array->count, esize, cast(Cmp, cmp));
    return p ? (cast(U8*, p) - array->data) / esize : ARRAY_NIL_IDX;
}

Int uarray_cmp_u8  (Void *A, Void *B) { U8  a = *cast(U8*,  A), b = *cast(U8*,  B); return (a < b) ? -1 : (a > b) ? 1 : 0; }
Int uarray_cmp_u32 (Void *A, Void *B) { U64 a = *cast(U64*, A), b = *cast(U64*, B); return (a < b) ? -1 : (a > b) ? 1 : 0; }
Int uarray_cmp_u64 (Void *A, Void *B) { U64 a = *cast(U64*, A), b = *cast(U64*, B); return (a < b) ? -1 : (a > b) ? 1 : 0; }
