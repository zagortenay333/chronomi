#pragma once

#include <string.h>
#include <stdlib.h>
#include "base/core.h"

// =============================================================================
// Base interface:
// ---------------
//
// - Allocators use assert_always for handling most errors.
// - Attempting to allocate 0 bytes is an error.
// - Calling grow on a NULL pointer behaves like calling alloc.
// - An alignment of 0 is interpreted as MAX_ALIGN.
//
// Usage example:
// --------------
//
// For convenience use the mem_* macros instead of Mem.op which
// pass the Mem struct itself as the context argument to Mem.op:
//
//     Auto foo = mem_alloc(mem, Foo, .zeroed=true, .size=(3 * sizeof(Foo)));
//     Auto bar = mem_new(mem, Bar);
//
// =============================================================================
ienum (MemOpTag, U8) {
    MEM_OP_FREE,
    MEM_OP_GROW,
    MEM_OP_ALLOC,
    MEM_OP_SHRINK,
};

istruct (MemOp) {
    MemOpTag tag;
    Bool zeroed;
    U64 size;
    U64 align;
    U64 old_size;
    Void *old_ptr;
};

istruct (Mem) {
    Void *(*op) (Void *context, MemOp);
};

#define mem_fn(M)              typematch(M, Mem*:cast(Mem*, M)->op, CMem*:cmem_op, Arena*:arena_op, TMem*:tmem_op)
#define mem_base(M)            ({ typematch(M, Mem*:0, CMem*:0, Arena*:0, TMem*:0); cast(Mem*, M); })
#define mem_op(M, T, TAG, ...) ({ def1(m, M); cast(T*, mem_fn(m)(m, (MemOp){__VA_ARGS__, .tag=(TAG)})); })
#define mem_new(M, T)          mem_op(M, T, MEM_OP_ALLOC, .zeroed=true, .align=alignof(T), .size=sizeof(T))
#define mem_alloc(M, T, ...)   mem_op(M, T, MEM_OP_ALLOC, __VA_ARGS__)
#define mem_grow(M, T, ...)    mem_op(M, T, MEM_OP_GROW, __VA_ARGS__)
#define mem_shrink(M, T, ...)  mem_op(M, T, MEM_OP_SHRINK, __VA_ARGS__)
#define mem_free(M, ...)       mem_op(M, Void, MEM_OP_FREE, __VA_ARGS__)

extern Mem *mem_root;

// =============================================================================
// CMem: wrapper around libc functions malloc, realloc, ...
// =============================================================================
istruct (CMem) {
    Mem mem;
};

Void *cmem_op (Void *cmem, MemOp);

// =============================================================================
// Arena:
// ------
//
// This allocator grabs a block of memory and treats it as a stack.
// That is, allocation is performed simply by incrementing a cursor.
//
// When the block runs out of space, a new block is allocated and
// linked with the previous one.
// =============================================================================
istruct (ArenaBlock) {
    ArenaBlock *prev;
    U64 capacity;
};

istruct (Arena) {
    Mem base;
    Mem *parent;
    ArenaBlock *block; // Current block from which we allocate.
    U64 block_count;   // Amount of bytes used in Arena.block including header.
    U64 total_count;   // Arena.block_count + capacities of all prev blocks.
    U64 min_block_size;
};

// The ArenaBlock struct is embedded at the start of a block
// and is added to Arena.block_count and ArenaBlock.capacity.
#define ARENA_BLOCK_HEADER sizeof(ArenaBlock)

Void  *arena_op      (Void *arena, MemOp);
Arena *arena_new     (Mem *, U64 min_block_size);
Void   arena_init    (Arena *, Mem *, U64 min_block_size);
Void   arena_destroy (Arena *);
Void  *arena_alloc   (Arena *, MemOp);
Void  *arena_grow    (Arena *, MemOp);
Void   arena_pop_to  (Arena *, U64 new_count);
Void   arena_pop_all (Arena *);

// =============================================================================
// TMem:
// -----
//
// The (T)emporary (Mem)ory allocator is used for easily
// obtaining scratch space using thread local variables:
//
//     Void print_stack_trace () {
//         tmem_new(tm); // Var tm freed at scope exit.
//         String s = build_stack_trace(tm, 4, 1);
//         printf("%.*s", STR(s));
//     }
//
// Init the TMem system per thread using tmem_setup().
//
// Arena fragmentation, ring buffer and pinning:
// ---------------------------------------------
//
// Each TMem allocator is backed by a thread local arena which
// is a stack. If 2 TMem's are backed by the same arena, it's
// possible the memory of one of them can't be popped because
// it's not entirely at the top of the stack. In this case the
// backing arena will have some fragmentation.
//
// Although arenas will naturally defragment because they are
// stacks, we still try to minimize that fragmentation using
// 2 mechanisms: a ring buffer and pinning.
//
// First, we use a ring buffer of 8 arenas where each call to
// tmem_new returns a TMem backed by the next arena in the
// ring. This keeps them disjoint in most cases.
//
// An edge case where the ring buffer alone fails is a module
// (for example: a recursive descent parser) with a bunch of
// recursive functions that opened more than 8 TMem's at the
// same time. Here it's possible a function has a local TMem
// backed by the same arena as the root level result arena.
//
// To deal with edge cases you can pin an arena (such as a
// root level result arena) which causes tmem_new to avoid
// using it as long as there are unpinned arenas available:
//
//     Void f (Mem *out) {
//         tmem_pin(out, 1); // Now only out is pinned.
//         tmem_new(tm);     // Begin new scratch space.
//         tmem_pin(tm, 0);  // Now out and tm are pinned.
//                           // Pins get reset at scope exit.
//     }
//
// =============================================================================
istruct (TMem) {
    Mem base;
    U64 count;
    U64 arena_pos;
    U8  slot_idx;
};

istruct (TMemRing) {
    U8 slot_idx;
    U8 pin_flags;
    Arena slots[8];
};

extern tls TMemRing tmem_ring;

#define tmem_new(N)      cleanup(tmem_destroy) TMem _##N; tmem_start(&_##N); Mem *N = cast(Mem*, &_##N);
#define tmem_pin(M, ...) cleanup(tmem_pin_pop) U8 JOIN(_, __LINE__) = tmem_pin_push(M, __VA_ARGS__);

Void *tmem_op       (Void *tmem, MemOp);
Void  tmem_setup    (Mem *, U64 min_total_size);
Void  tmem_start    (TMem *);
Void  tmem_destroy  (TMem *);
U8    tmem_pin_push (Mem *, Bool exclusive);
Void  tmem_pin_pop  (U8 *);
