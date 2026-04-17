#include "base/mem.h"
#include "base/tpool.h"
#include "os/threads.h"

istruct (Task) {
    Task *next;
    TPoolFn *fn;
    Void *fn_arg;
};

istruct (Worker) {
    U64 id;
    TPool *pool;
    OsThread *thread;
};

array_typedef(Task, Task);
array_typedef(Worker, Worker);

istruct (TPool) {
    Mem *mem;

    ArrayTask task_ring;
    U64 task_ring_count;
    U64 task_ring_cursor;

    OsMutex *mutex;     // Protects all of TPool.
    OsCondVar *task_cv; // Signal that new tasks have arrived.
    OsCondVar *push_cv; // Signal that task queue is not full.
    OsCondVar *done_cv; // Signal that we are out of work or stopped.

    ArrayWorker workers;
    U64 worker_count;
    U64 working_count; // Number of workers running their TPoolFn.
    Bool pending_push;
    Bool stop;
};

static Void worker (Void *arg) {
    Auto w  = cast(Worker*, arg);
    Auto tp = w->pool;

    while (true) {
        os_mutex_lock(tp->mutex);
        while (!tp->stop && (tp->task_ring_count == 0)) os_cond_var_wait(tp->task_cv, tp->mutex);
        if (tp->stop) break;
        Auto task = array_get(&tp->task_ring, tp->task_ring_cursor);
        tp->task_ring_cursor = (tp->task_ring_cursor + 1) % tp->task_ring.count;
        tp->task_ring_count--;
        tp->working_count++;
        os_cond_var_signal(tp->push_cv);
        os_mutex_unlock(tp->mutex);

        task.fn(task.fn_arg, w->id);

        os_mutex_lock(tp->mutex);
        tp->working_count--;
        if (!tp->stop && !tp->task_ring_count && (tp->working_count == 0)) os_cond_var_signal(tp->done_cv);
        os_mutex_unlock(tp->mutex);
    }

    tp->worker_count--;
    if (tp->worker_count == 0) {
        os_cond_var_signal(tp->done_cv);
        os_cond_var_signal(tp->push_cv);
    }
    os_mutex_unlock(tp->mutex);
}

// The allocator passed in here does not have to be thread safe,
// or owned by the pool since only this function will touch it.
TPool *tpool_new (Mem *mem, U64 worker_count, U64 queue_size) {
    TPool *tp        = mem_new(mem, TPool);
    tp->mem          = mem;
    tp->worker_count = worker_count ?: 2;
    tp->mutex        = os_mutex_new(mem);
    tp->task_cv      = os_cond_var_new(mem);
    tp->push_cv      = os_cond_var_new(mem);
    tp->done_cv      = os_cond_var_new(mem);

    array_init(&tp->task_ring, mem);
    array_ensure_count(&tp->task_ring, queue_size, 0);

    array_init(&tp->workers, mem);
    array_ensure_count(&tp->workers, worker_count, 0);

    array_iter (w, &tp->workers, *) {
        w->id     = ARRAY_IDX;
        w->pool   = tp;
        w->thread = os_thread_new(mem, worker, w);
        os_thread_detach(w->thread);
    }

    return tp;
}

// This function might suspend the caller until currently
// running tasks are finished. Tasks that are waiting in
// the queue to be run will be removed.
Void tpool_destroy (TPool *tp) {
    os_mutex_lock(tp->mutex);
    tp->stop = true;
    os_cond_var_broadcast(tp->task_cv);
    os_mutex_unlock(tp->mutex);

    tpool_wait(tp);

    os_cond_var_destroy(tp->task_cv, tp->mem);
    os_cond_var_destroy(tp->done_cv, tp->mem);
    os_mutex_destroy(tp->mutex, tp->mem);
    mem_free(tp->mem, .old_ptr=tp, .old_size=sizeof(TPool));
}

// IMPORTANT: Don't call this function from within TPoolFn
// because it can deadlock. This is because this function
// might suspend the caller if the internal queue is full.
Void tpool_push (TPool *tp, TPoolFn fn, Void *fn_arg) {
    os_mutex_lock(tp->mutex);
    tp->pending_push = true;
    while (!tp->stop && (tp->task_ring_count == tp->task_ring.count)) os_cond_var_wait(tp->push_cv, tp->mutex);
    if (! tp->stop) {
        Auto task = array_ref(&tp->task_ring, (tp->task_ring_cursor + tp->task_ring_count) % tp->task_ring.count);
        task->fn = fn;
        task->fn_arg = fn_arg;
        tp->task_ring_count++;
        os_cond_var_signal(tp->task_cv);
    }
    tp->pending_push = false;
    os_mutex_unlock(tp->mutex);
}

// Suspends caller until all work is done. Also used
// internally to wait until all threads have shut down.
Void tpool_wait (TPool *tp) {
    os_mutex_lock(tp->mutex);
    while (tp->task_ring_count || tp->pending_push || (!tp->stop && tp->working_count) || (tp->stop && tp->worker_count)) {
        os_cond_var_wait(tp->done_cv, tp->mutex);
    }
    os_mutex_unlock(tp->mutex);
}

// Split range [0, n] into m roughly even ranges of the
// form [a, b) where m is the number of worker threads.
SliceRangeU64 tpool_split (TPool *tp, Mem *mem, U64 n) {
    SliceRangeU64 ranges = { .data=mem_alloc(mem, RangeU64, .size=(tp->worker_count * sizeof(RangeU64))), .count=tp->worker_count };
    U64 w = ceil_div(n, tp->worker_count);
    array_iter (r, &ranges, *) { r->a = min(n, ARRAY_IDX*w); r->b = min(n, ARRAY_IDX*w+w); }
    return ranges;
}
