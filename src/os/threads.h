#pragma once

#include "base/core.h"
#include "base/mem.h"

// =============================================================================
// Threads:
// =============================================================================
typedef Void (*OsThreadFn)(Void *);

istruct (OsThread) {
    OsThreadFn fn;
    Void *fn_arg;
};

OsThread *os_thread_new     (Mem *, OsThreadFn, Void *fn_arg);
Void      os_thread_destroy (OsThread *, Mem *);
Bool      os_thread_join    (OsThread *);
Void      os_thread_detach  (OsThread *);

// =============================================================================
// Mutex:
// =============================================================================
istruct (OsMutex) { U8 _; };

OsMutex *os_mutex_new     (Mem *);
Void     os_mutex_destroy (OsMutex *, Mem *);
Void     os_mutex_lock    (OsMutex *);
Void     os_mutex_unlock  (OsMutex *);

inl Void os_mutex_unlock_ (OsMutex **m) { os_mutex_unlock(*m); }
#define  os_mutex_scoped_lock(M) cleanup(os_mutex_unlock_) OsMutex *JOIN(_, __LINE__) = (M); os_mutex_lock(JOIN(_, __LINE__));

// =============================================================================
// Read/Write lock:
// =============================================================================
istruct (OsRwMutex) { U8 _; };

OsRwMutex *os_rw_mutex_new     (Mem *);
Void       os_rw_mutex_destroy (OsRwMutex *, Mem *);
Void       os_rw_mutex_take_r  (OsRwMutex *);
Void       os_rw_mutex_drop_r  (OsRwMutex *);
Void       os_rw_mutex_take_w  (OsRwMutex *);
Void       os_rw_mutex_drop_w  (OsRwMutex *);

// =============================================================================
// Conditional variable:
// =============================================================================
istruct (OsCondVar) { U8 _; };

OsCondVar *os_cond_var_new       (Mem *);
Void       os_cond_var_destroy   (OsCondVar *, Mem *);
Void       os_cond_var_wait      (OsCondVar *, OsMutex *);
Void       os_cond_var_signal    (OsCondVar *);
Void       os_cond_var_broadcast (OsCondVar *);

// =============================================================================
// Semaphore:
// =============================================================================
istruct (OsSemaphore) { U8 _; };

OsSemaphore *os_semaphore_new     (Mem *, U32);
Void         os_semaphore_destroy (OsSemaphore *, Mem *);
Void         os_semaphore_wait    (OsSemaphore *);
Void         os_semaphore_post    (OsSemaphore *);
