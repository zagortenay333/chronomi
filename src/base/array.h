#pragma once

// =============================================================================
// Overview:
// ---------
//
// A polymorphic dynamic array with bounds checking.
//
// All macros here eval their arguments only once.
//
// To ensure bounds checking, elements should only be accessed
// with provided methods such as: array_get, array_set, etc...
//
// The array calls mem_grow/mem_shrink which can invalidate
// slices/pointers to it's elements depending on the allocator.
//
// Usage example:
// --------------
//
//     Array(U64) a;
//     array_init(&a, mem);
//     array_push_n(&a, 42, 1, 420);
//     array_iter (x, &a, *) if (*x == 420) *x = 1000;
//
// Careful when mutating the array while looping over it.
// If looping *forward* you can append to the array as well
// as remove the current element:
//
//     array_iter (x, &a) if (x == 1) array_remove(&a, ARRAY_IDX--);
//     array_iter (x, &a) printf("[%lu] = %lu\n", ARRAY_IDX, x);
//
// =============================================================================
#include "base/mem.h"

#define SliceBase(...) struct { __VA_ARGS__ *data; U64 count; }
#define ArrayBase(...) struct { __VA_ARGS__ *data; U64 count; U64 capacity; Mem *mem; }

typedef ArrayBase(U8) UArray;
typedef SliceBase(U8) USlice;

#define Slice(...) union { SliceBase(__VA_ARGS__); USlice as_uslice; }
#define Array(...) union { ArrayBase(__VA_ARGS__); USlice as_uslice; UArray as_uarray; Slice(__VA_ARGS__) as_slice; }
#define AElem(...) Type((__VA_ARGS__)->data[0])

#define array_typedef(T, S)\
    typedef Array(T) Array##S;\
    typedef Type(cast(Array##S*, 0)->as_slice) Slice##S;

array_typedef(U8, U8);
array_typedef(U32, U32);
array_typedef(I32, I32);
array_typedef(U64, U64);
array_typedef(Bool, Bool);
array_typedef(Void*, Void);
array_typedef(Char, Char);
array_typedef(CString, CString);
array_typedef(RangeU64, RangeU64);

#define ARRAY_NIL_IDX UINT32_MAX

Void   uarray_maybe_decrease_capacity      (UArray *, U64 esize);
Void   uarray_increase_capacity            (UArray *, U64 esize, U64 n);
Void   uarray_ensure_capacity              (UArray *, U64 esize, U64 n);
Void   uarray_ensure_capacity_min          (UArray *, U64 esize, U64 n);
Void   uarray_increase_count               (UArray *, U64 esize, U64 n, Bool zeroed, USlice *out);
Void   uarray_ensure_count                 (UArray *, U64 esize, U64 n, Bool zeroed);
Void  *uarray_push                         (UArray *, U64 esize);
Void  *uarray_insert                       (UArray *, U64 esize, U64 idx);
Void   uarray_push_many                    (UArray *, USlice *, U64 esize);
Void   uarray_insert_many                  (UArray *, USlice *, U64 esize, U64 idx);
USlice uarray_insert_gap                   (UArray *, U64, U64 count, U64 idx, Bool zeroed);
Void   uarray_remove                       (UArray *, U64 esize, U64 idx);
Void   uarray_remove_many                  (UArray *, U64 esize, U64 idx, U64 n);
Void   uarray_sort                         (UArray *, U64 esize, Int(*)(Void*, Void*));
U64    uarray_bsearch                      (UArray *, U64 esize, Void *, Int(*)(Void*, Void*));
Int    uarray_cmp_u8                       (Void *, Void *);
Int    uarray_cmp_u32                      (Void *, Void *);
Int    uarray_cmp_u64                      (Void *, Void *);

#define array_init(A, MEM)                 (*(A) = (Type(*(A))){ .mem = mem_base(MEM) })
#define array_init_cap(A, MEM, CAP)        ({ def3(a, m, c, A, MEM, CAP); array_init(a, m); array_increase_capacity(a, c); })
#define array_free(A)                      ({ def1(a, A); mem_free(a->mem, .old_ptr=a->data, .old_size=array_size(a)); })

#define uslice_from(A)                     (&(A)->as_uslice)
#define uarray_from(A)                     (&(A)->as_uarray)
#define slice_from(A, T)                   (*cast(T*, &(A)->as_slice))
#define uslice_static(S)                   (&(USlice){ .data=cast(U8*, S), .count=sizeof(S)/sizeof(*(S)) })
#define array_esize(A)                     (sizeof(AElem(A)))
#define array_size(A)                      (array_esize(A) * (A)->count)
#define array_cmp_fn(A)                    typematch(AElem(A), U8:uarray_cmp_u8, U32:uarray_cmp_u32, U64:uarray_cmp_u64)

#define array_bounds_check(A, I)           assert_always((I) < (A)->count)
#define array_ref(A, I)                    ({ def2(a, i, A, acast(U64,I)); array_bounds_check(a, i); &a->data[i]; })
#define array_get(A, I)                    (*array_ref(A, I))
#define array_set(A, I, V)                 (*array_ref(A, I) = (V))
#define array_try_ref(A, I)                ({ def2(a, i, A, acast(U64,I)); (i < a->count) ? &a->data[i] : 0; })
#define array_try_get(A, I)                ({ def2(a, i, A, acast(U64,I)); (i < a->count) ? a->data[i] : (AElem(a)){}; })
#define array_ref_last(A)                  ({ def1(a, A); array_bounds_check(a, 0); &a->data[a->count - 1]; })
#define array_get_last(A)                  (*array_ref_last(A))
#define array_set_last(A, V)               (*array_ref_last(A) = (V))
#define array_try_ref_last(A)              ({ def1(a, A); a->count ? &a->data[a->count - 1] : 0; })
#define array_try_get_last(A)              ({ def1(a, A); a->count ? a->data[a->count - 1] : (AElem(a)){}; })

#define array_maybe_decrease_capacity(A)   uarray_maybe_decrease_capacity(uarray_from(A), array_esize(A));
#define array_increase_capacity(A, N)      uarray_increase_capacity(uarray_from(A), array_esize(A), N);
#define array_ensure_capacity(A, N)        uarray_ensure_capacity(uarray_from(A), array_esize(A), N);
#define array_ensure_capacity_min(A, N)    uarray_ensure_capacity_min(uarray_from(A), array_esize(A), N);
#define array_ensure_count(A, N, Z)        uarray_ensure_count(uarray_from(A), array_esize(A), N, Z);
#define array_increase_count(A, N, Z)      uarray_increase_count(uarray_from(A), array_esize(A), N, Z, 0);
#define array_increase_count_o(A, N, Z, O) uarray_increase_count(uarray_from(A), array_esize(A), N, Z, uslice_from(O));

#define array_push(A, E)                   (*cast(AElem(A)*, uarray_push(uarray_from(A), array_esize(A))) = E)
#define array_push_slot(A)                 cast(AElem(A)*, uarray_push(uarray_from(A), array_esize(A)))
#define array_insert(A, E, I)              (*cast(AElem(A)*, uarray_insert(uarray_from(A), array_esize(A), I)) = E)
#define array_push_lit(A, ...)             array_push(A, ((AElem(A)){__VA_ARGS__}))
#define array_insert_lit(A, I, ...)        array_insert(A, ((AElem(A)){ __VA_ARGS__ }), I)
#define array_push_if_unique(A, E)         ({ def2(a, e, A, acast(AElem(A), E)); if (! array_has(a, e)) array_push(a, e); })
#define array_push_n(A, ...)               ({ AElem(A) _(E)[] = {__VA_ARGS__};  uarray_push_many(uarray_from(A), uslice_static(_(E)), array_esize(A)); })
#define array_push_many(A, ES)             ({ typematch(AElem(A), AElem(ES):0); uarray_push_many(uarray_from(A), uslice_from(ES), array_esize(A)); })
#define array_insert_many(A, ES, I)        ({ typematch(AElem(A), AElem(ES):0); uarray_insert_many(uarray_from(A), uslice_from(ES), array_esize(A), (I)); })
#define array_insert_gap(A, N, I, Z)       ({ Auto _(R) = uarray_insert_gap(uarray_from(A), array_esize(A), (N), (I), (Z)); *cast(Slice(AElem(A))*, &_(R)); })

#define array_pop(A)                       ({ def1(a, A); AElem(a) e = array_get_last(a); a->count--; e; })
#define array_pop_or(A, OR)                ({ def2(a, v, A, acast(AElem(A), OR)); a->count ? array_pop(a) : v; })
#define array_remove(A, I)                 uarray_remove(uarray_from(A), array_esize(A), I);
#define array_remove_many(A, I, N)         uarray_remove_many(uarray_from(A), array_esize(A), I, N);
#define array_remove_fast(A, I)            ({ def2(a, i, A, I); array_set(a, i, array_get_last(a)); a->count--; })
#define array_swap_remove(A, I)            ({ def2(a, i, A, I); array_swap(a, i, a->count-1); a->count--; })

#define array_swap(A, I, J)                ({ def3(a, i, j, A, I, J); AElem(a) *e1=array_ref(a,i), *e2=array_ref(a,j), tmp=*e1; *e1=*e2; *e2=tmp; })
#define array_reverse(A)                   ({ def1(a, A); for (U64 i=0; i < a->count/2; ++i) array_swap(a, i, a->count-i-1); })
#define array_shuffle(A)                   array_iter (x, A) { cast(Void, x); swap(ARRAY->data[ARRAY_IDX], ARRAY->data[random_range(ARRAY_IDX, ARRAY->count)]); }
#define array_sort(A)                      uarray_sort(uarray_from(A), array_esize(A), array_cmp_fn(A));
#define array_sort_cmp(A, CMP)             uarray_sort(uarray_from(A), array_esize(A), CMP);

#define array_has(A, E)                    ({ def2(a, e, A, acast(AElem(A), E)); !!array_find_ref(a, e == *IT); })
#define array_find(A, C)                   ({ U64 _(R) = ARRAY_NIL_IDX; array_iter (IT, A)    if (C) { _(R) = ARRAY_IDX; break; } _(R); })
#define array_find_get(A, C)               ({ AElem(A)  _(R) = {};      array_iter (IT, A)    if (C) { _(R) = IT; break; }        _(R); })
#define array_find_ref(A, C)               ({ AElem(A) *_(R) = 0;       array_iter (IT, A, *) if (C) { _(R) = IT; break; }        _(R); })
#define array_find_remove(A, C)            array_iter (IT, A) if (C) { array_remove(ARRAY, ARRAY_IDX); break; }
#define array_find_remove_fast(A, C)       array_iter (IT, A) if (C) { array_remove_fast(ARRAY, ARRAY_IDX); break; }
#define array_find_remove_all_fast(A, C)   array_iter_back (IT, A) if (C) array_remove_fast(ARRAY, ARRAY_IDX);
#define array_find_replace(A, C, R)        array_iter (IT, A) if (C) { ARRAY->data[ARRAY_IDX] = R; break; }
#define array_find_replace_all(A, C, R)    array_iter (IT, A) if (C) ARRAY->data[ARRAY_IDX] = R;
#define array_find_remove_all(A, C)        ({ def1(A_, A); U64 _(N)=0; array_iter (IT, A_) if (!(C)) { A_->data[_(N)++]=IT; } A_->count=_(N); })
#define array_bsearch(A, E)                ({ def2(a, e, A, acast(AElem(A), E)); uarray_bsearch(uarray_from(a), array_esize(a), &e, array_cmp_fn(a)); })
#define array_bsearch_cmp(A, E, CMP)       ({ def2(a, e, A, acast(AElem(A), E)); uarray_bsearch(uarray_from(a), array_esize(a), &e, CMP); })

#define array_iter(X, A, ...)              let1(ARRAY, A)        ARRAY_ITER(X, 0, (ARRAY_IDX < ARRAY->count), ++ARRAY_IDX, __VA_ARGS__)
#define array_iter_from(X, A, I, ...)      let2(ARRAY, I_, A, I) ARRAY_ITER(X, I_, (ARRAY_IDX < ARRAY->count), ++ARRAY_IDX, __VA_ARGS__)
#define array_iter_back(X, A, ...)         let1(ARRAY, A)        ARRAY_ITER(X, ARRAY->count, (ARRAY_IDX-- > 0), /**/, __VA_ARGS__)
#define array_iter_back_from(X, A, I, ...) let2(ARRAY, I_, A, I) ARRAY_ITER(X, (ARRAY->count ? I_+1 : 0), (ARRAY_IDX-- > 0), /**/, __VA_ARGS__)
#define ARRAY_ITER_DONE                    (ARRAY_IDX == (ARRAY->count - 1))
#define ARRAY_ITER(X, F, C, INC, ...)      for (U64 ARRAY_IDX=(F), _(I)=1; _(I); _(I)=0)\
                                           for (AElem(ARRAY) __VA_ARGS__ X; (C) && (X = __VA_OPT__(&)ARRAY->data[ARRAY_IDX], true); INC)
