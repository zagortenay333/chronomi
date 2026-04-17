#pragma once

// =============================================================================
// Overview:
// ---------
//
// A simple thread pool with a bounded queue for tasks.
//
// IMPORTANT: The tpool_push function should not be called
// from within TPoolFn as it can put the worker thread to
// sleep causing deadlock.
//
// Usage example:
// --------------
//
//     TPOOL_FN(worker) {
//         os_sleep_ms(500);
//         printf("id=%lu val=%lu\n", worker_id, cast(U64, arg));
//     }
//     
//     Void test () {
//         tmem_new(tm);
//         Auto tp = tpool_new(tm, 2, 1*KB);
//         for (U64 i = 0; i < 10; ++i) tpool_push(tp, worker, cast(Void*, i));
//         tpool_wait(tp);
//         printf("Done!\n");
//         tpool_destroy(tp);
//     }
//
// =============================================================================
#include "base/core.h"
#include "base/array.h"

#define TPOOL_FN(NAME) Void NAME (Void *arg, U64 worker_id)
typedef TPOOL_FN(TPoolFn);

istruct (TPool);

TPool        *tpool_new     (Mem *, U64 worker_count, U64 queue_size);
Void          tpool_destroy (TPool *);
Void          tpool_push    (TPool *, TPoolFn, Void *fn_arg);
Void          tpool_wait    (TPool *);
SliceRangeU64 tpool_split   (TPool *, Mem *, U64);
