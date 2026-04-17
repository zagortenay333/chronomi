#pragma once

#include <stdio.h>
#include <stdbit.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdckdint.h>
#include "base/context.h"

typedef int8_t    I8;
typedef int16_t   I16;
typedef int32_t   I32;
typedef int64_t   I64;
typedef uint8_t   U8;
typedef uint16_t  U16;
typedef uint32_t  U32;
typedef uint64_t  U64;
typedef uintptr_t UIntPtr;
typedef float     F32;
typedef double    F64;
typedef int       Int;
typedef size_t    Size;
typedef char      Char;
typedef bool      Bool;
typedef void      Void;
typedef Char     *CString;
typedef va_list   VaList;

#define TERM_END           "\x1b[0m"
#define TERM_START_BLACK   "\x1b[30m"
#define TERM_START_RED     "\x1b[31m"
#define TERM_START_GREEN   "\x1b[32m"
#define TERM_START_YELLOW  "\x1b[33m"
#define TERM_START_BLUE    "\x1b[34m"
#define TERM_START_MAGENTA "\x1b[35m"
#define TERM_START_CYAN    "\x1b[36m"
#define TERM_START_WHITE   "\x1b[37m"
#define TERM_START_BOLD    "\x1b[1m"
#define TERM_BLACK(TXT)    TERM_START_BLACK   TXT TERM_END
#define TERM_RED(TXT)      TERM_START_RED     TXT TERM_END
#define TERM_GREEN(TXT)    TERM_START_GREEN   TXT TERM_END
#define TERM_YELLOW(TXT)   TERM_START_YELLOW  TXT TERM_END
#define TERM_BLUE(TXT)     TERM_START_BLUE    TXT TERM_END
#define TERM_MAGENTA(TXT)  TERM_START_MAGENTA TXT TERM_END
#define TERM_CYAN(TXT)     TERM_START_CYAN    TXT TERM_END
#define TERM_WHITE(TXT)    TERM_START_WHITE   TXT TERM_END
#define TERM_BOLD(TXT)     TERM_START_BOLD    TXT TERM_END

#define KB        (1024u)
#define MB        (1024u*KB)
#define GB        (1024u*MB)
#define MAX_ALIGN (alignof(max_align_t))

#define JOIN_(A, B) A ## B
#define JOIN(A, B)  JOIN_(A, B)

#if BUILD_DEBUG
    #define assert_dbg(...) ({ if (!(__VA_ARGS__)) panic(); })
    Void print_stack_trace ();
    #define panic() ({ print_stack_trace(); __builtin_trap(); })
#else
    #define assert_dbg(...)
    #define panic() __builtin_trap()
#endif

#define iunion(N, ...)        typedef union N N; union [[__VA_ARGS__]] N
#define istruct(N, ...)       typedef struct N N; struct [[__VA_ARGS__]] N
#define ienum(N, T, ...)      enum N:T; typedef enum N N; enum [[__VA_ARGS__]] N:T
#define fenum(N, T, ...)      typedef T N; enum [[__VA_ARGS__]]
#define inl                   static inline
#define tls                   thread_local
#define Noreturn              [[noreturn]]
#define Auto                  auto
#define Fmt(FMT, VA)          [[gnu::format(printf, FMT, VA)]]
#define Type(X)               typeof(X)
#define typematch(A, ...)     _Generic(*cast(Type(A)*, 0), __VA_ARGS__)
#define typematch2(A, B, ...) _Generic(cast(Void(*)(Type(A), Type(B)), 0), __VA_ARGS__)
#define cleanup(FN)           [[gnu::cleanup(FN)]]
#define cast(T, V)            ((T)(V))
#define acast(T, V)           ({ T __(v) = (V); __(v); })
#define PACKED                gnu::packed
#define assert_always(...)    ({ if (!(__VA_ARGS__)) panic(); })
#define assert_static(...)    static_assert(__VA_ARGS__)
#define badpath               panic()
#define through               [[fallthrough]]
#define flag(N)               (1u << (N))
#define swap(A, B)            ({ def2(a, b, &(A), &(B)); Type(*a) t=*a; *a=*b; *b=t; })
#define _(N)                  N##__
#define __(N)                 N##___ // Must be different from _() and used mainly in the def macros.
#define nop(...)
#define identity(...)         __VA_ARGS__
#define popcount(N)           stdc_count_ones(N)
#define is_pow2(N)            stdc_has_single_bit(N)
#define leading_one_bits(N)   stdc_leading_ones(N)
#define next_pow2(X)          ({ def1(r, stdc_bit_ceil(typematch(X, U32:X, U64:X))); assert_dbg(r); r; })
#define min(A, B)             ({ def2(a, b, A, B); (a < b) ? a : b; })
#define max(A, B)             ({ def2(a, b, A, B); (a > b) ? a : b; })
#define clamp(X, A, B)        ({ def3(x, a, b, X, A, B); (x < a) ? a : (x > b) ? b : x; })
#define ceil_div(A, B)        ({ def2(a, b, A, B); (a + b - 1) / b; })
#define safe_add(A, B)        ({ def2(a, b, A, B); Type(a) r; assert_always(! ckd_add(&r, a, b)); r; })
#define safe_sub(A, B)        ({ def2(a, b, A, B); Type(a) r; assert_always(! ckd_sub(&r, a, b)); r; })
#define safe_mul(A, B)        ({ def2(a, b, A, B); Type(a) r; assert_always(! ckd_mul(&r, a, b)); r; })

// Use the def/let macros to avoid evaling macro args multiple times.
// The args are first moved to name-mangled vars then the final ones.
// This way the passed-in snippets only see mangled identifers. This
// will be robust as long as the __() macro is used mostly in here.
//
//     def1(a, 42);
//     def1(b, acast(U8, X));
//     def2(foo, bar, acast(U8, x), 64);
//
// The let macros are similar but use run-once for loops to make the
// definitions scoped. The loops are easily optimized away:
//
//     let1 (a, 1)  { The a var is only visible here. }
//
#define def1_(W, A, AV)\
    W(Type(AV) __(A) = (AV);)\
    W(Type(__(A)) A = __(A);)
#define def2_(W, A, B, AV, BV)\
    W(Type(AV) __(A) = (AV);) W(Type(BV) __(B) = (BV);)\
    W(Type(__(A)) A = __(A);) W(Type(__(B)) B = __(B);)
#define def3_(W, A, B, C, AV, BV, CV)\
    W(Type(AV) __(A) = (AV);) W(Type(BV) __(B) = (BV);) W(Type(CV) __(C) = (CV);)\
    W(Type(__(A)) A = __(A);) W(Type(__(B)) B = __(B);) W(Type(__(C)) C = __(C);)

#define def1(...) def1_(identity, __VA_ARGS__)
#define def2(...) def2_(identity, __VA_ARGS__)
#define def3(...) def3_(identity, __VA_ARGS__)
#define let_(...) for (__VA_ARGS__ _(I);)
#define let1(...) for (U8 _(I)=1; _(I);) def1_(let_, __VA_ARGS__) for (; _(I); _(I)=0)
#define let2(...) for (U8 _(I)=1; _(I);) def2_(let_, __VA_ARGS__) for (; _(I); _(I)=0)

// Use the reach/reached macros to mark a position in
// code that must be reached before exiting a scope:
//
//     Foo foo () {
//         reach(r);
//         #define RETURN(R) { reached(r); return r; }
//         if (...) RETURN(a);
//         else     return b; // Triggers assert_dbg().
//     }
//
#if BUILD_DEBUG
    #define  reach(N)   cleanup(reached_) I64 _(N##REACH) = 1;
    #define  reached(N) _(N##REACH) = 0;
    inl Void reached_ (I64 *g) { assert_dbg(*g == 0); }
#else
    #define reach(N)
    #define reached(N)
#endif

istruct (RangeU8)  { U8  a, b; };
istruct (RangeU32) { U32 a, b; };
istruct (RangeU64) { U64 a, b; };

// Counts number of digits base 10.
U8 count_digits (U64);

// Returns min n where x+n is multiple of a.
U64 padding_to_align (U64 x, U64 a);

// Integer hashing.
U64 hash_u32 (U32);
U64 hash_u64 (U64);
U64 hash_i32 (I32);
U64 hash_i64 (I64);

// Bitwise left rotate.
U8  rotl8  (U8  x, U64 r);
U32 rotl32 (U32 x, U64 r);
U64 rotl64 (U64 x, U64 r);

// Saturating ops.
U8  sat_add8  (U8, U8);
U32 sat_add32 (U32, U32);
U64 sat_add64 (U64, U64);
U8  sat_sub8  (U8, U8);
U32 sat_sub32 (U32, U32);
U64 sat_sub64 (U64, U64);
U8  sat_mul8  (U8, U8);
U32 sat_mul32 (U32, U32);
U64 sat_mul64 (U64, U64);

// Pseudo random number generator.
Void random_setup (); // Per thread init.
U64  random_u64   ();
U64  random_range (U64 l, U64 u); // In range [l, u).
