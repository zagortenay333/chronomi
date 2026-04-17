#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include "os/threads.h"
#include "base/log.h"
#include "base/mem.h"

// =============================================================================
// Threads:
// =============================================================================
istruct (LinuxThread) {
    OsThread base;
    pthread_t handle;
};

static Void *thread_entry (Void *arg) {
    Auto thread = cast(LinuxThread*, arg);
    random_setup();
    tmem_setup(mem_root, 1*MB);
    log_setup(mem_root, 4*KB);
    thread->base.fn(thread->base.fn_arg);
    return 0;
}

OsThread *os_thread_new (Mem *mem, OsThreadFn fn, Void *fn_arg) {
    Auto thread = mem_new(mem, LinuxThread);
    thread->base.fn = fn;
    thread->base.fn_arg = fn_arg;
    Int r = pthread_create(&thread->handle, 0, thread_entry, thread);
    if (r == -1) {
        mem_free(mem, .old_ptr=thread, .old_size=sizeof(LinuxThread));
        return 0;
    }
    return cast(OsThread*, thread);
}

Void os_thread_destroy (OsThread *thread, Mem *mem) {
    Auto t = cast(LinuxThread*, thread);
    mem_free(mem, .old_ptr=t, .old_size=sizeof(LinuxThread));
}

Bool os_thread_join (OsThread *thread) {
    Auto t = cast(LinuxThread*, thread);
    Int r = pthread_join(t->handle, 0);
    return r == 0;
}

Void os_thread_detach (OsThread *thread) {
    Auto t = cast(LinuxThread*, thread);
    Int r  = pthread_detach(t->handle);
    assert_always(r == 0);
}

// =============================================================================
// Mutex:
// =============================================================================
istruct (LinuxMutex) {
    OsMutex base;
    pthread_mutex_t handle;
};

OsMutex *os_mutex_new (Mem *mem) {
    Auto mutex = mem_new(mem, LinuxMutex);
    Int r = pthread_mutex_init(&mutex->handle, 0);
    assert_always(r == 0);
    return cast(OsMutex*, mutex);
}

Void os_mutex_destroy (OsMutex *mutex, Mem *mem) {
    Auto m = cast(LinuxMutex*, mutex);
    pthread_mutex_destroy(&m->handle);
    mem_free(mem, .old_ptr=m, .old_size=sizeof(LinuxMutex));
}

Void os_mutex_lock (OsMutex *mutex) {
    Int r = pthread_mutex_lock(&cast(LinuxMutex*, mutex)->handle);
    assert_always(r == 0);
}

Void os_mutex_unlock (OsMutex *mutex) {
    Int r = pthread_mutex_unlock(&cast(LinuxMutex*, mutex)->handle);
    assert_always(r == 0);
}

// =============================================================================
// Read/Write lock:
// =============================================================================
istruct (LinuxRwMutex) {
    OsRwMutex base;
    pthread_rwlock_t handle;
};

OsRwMutex *os_rw_mutex_new (Mem *mem) {
    Auto rwm = mem_new(mem, LinuxRwMutex);
    Int r = pthread_rwlock_init(&rwm->handle, 0);
    if (r == -1) {
        mem_free(mem, .old_ptr=rwm, .old_size=sizeof(LinuxRwMutex));
        return 0;
    }
    return cast(OsRwMutex*, rwm);
}

Void os_rw_mutex_destroy (OsRwMutex *rwm, Mem *mem) {
    Int r = pthread_rwlock_destroy(&cast(LinuxRwMutex*, rwm)->handle);
    assert_always(r == 0);
}

Void os_rw_mutex_take_r (OsRwMutex *rwm) {
    Int r = pthread_rwlock_rdlock(&cast(LinuxRwMutex*, rwm)->handle);
    assert_always(r == 0);
}

Void os_rw_mutex_drop_r (OsRwMutex *rwm) {
    Int r = pthread_rwlock_unlock(&cast(LinuxRwMutex*, rwm)->handle);
    assert_always(r == 0);
}

Void os_rw_mutex_take_w (OsRwMutex *rwm) {
    Int r = pthread_rwlock_wrlock(&cast(LinuxRwMutex*, rwm)->handle);
    assert_always(r == 0);
}

Void os_rw_mutex_drop_w (OsRwMutex *rwm) {
    Int r = pthread_rwlock_unlock(&cast(LinuxRwMutex*, rwm)->handle);
    assert_always(r == 0);
}

// =============================================================================
// Conditional variable:
// =============================================================================
istruct (LinuxCondVar) {
    OsCondVar base;
    pthread_cond_t cond;
};

OsCondVar *os_cond_var_new (Mem *mem) {
    Auto cv = mem_new(mem, LinuxCondVar);
    if (-1 == pthread_cond_init(&cv->cond, 0)) {
        mem_free(mem, .old_ptr=cv, .old_size=sizeof(LinuxCondVar));
        return 0;
    }
    return cast(OsCondVar*, cv);
}

Void os_cond_var_destroy (OsCondVar *cv, Mem *mem) {
    Auto c = cast(LinuxCondVar*, cv);
    pthread_cond_destroy(&c->cond);
    mem_free(mem, .old_ptr=c, .old_size=sizeof(LinuxCondVar));
}

Void os_cond_var_wait (OsCondVar *cv, OsMutex *mutex) {
    Int r = pthread_cond_wait(&cast(LinuxCondVar*, cv)->cond, &cast(LinuxMutex*, mutex)->handle);
    assert_always(r == 0);
}

Void os_cond_var_signal (OsCondVar *cv) {
    Int r = pthread_cond_signal(&cast(LinuxCondVar*, cv)->cond);
    assert_always(r == 0);
}

Void os_cond_var_broadcast (OsCondVar *cv) {
    Int r = pthread_cond_broadcast(&cast(LinuxCondVar*, cv)->cond);
    assert_always(r == 0);
}

// =============================================================================
// Semaphore:
// =============================================================================
istruct (LinuxSemaphore) {
    OsSemaphore base;
    sem_t handle;
};

OsSemaphore *os_semaphore_new (Mem *mem, U32 init_count) {
    Auto sem = mem_new(mem, LinuxSemaphore);
    Int r = sem_init(&sem->handle, 0, init_count);
    assert_always(r == 0);
    return cast(OsSemaphore*, sem);
}

Void os_semaphore_destroy (OsSemaphore *sem, Mem *mem) {
    Auto s = cast(LinuxSemaphore*, sem);
    sem_destroy(&s->handle);
    mem_free(mem, .old_ptr=s, .old_size=sizeof(LinuxSemaphore));
}

Void os_semaphore_wait (OsSemaphore *sem) {
    while (true) {
        Int r = sem_wait(&cast(LinuxSemaphore*, sem)->handle);
        if (r == 0) break;
        if (errno == EAGAIN) continue;
        badpath;
    }
}

Void os_semaphore_post (OsSemaphore *sem) {
    while (true) {
        Int r = sem_post(&cast(LinuxSemaphore*, sem)->handle);
        if (r == 0) break;
        if (errno == EAGAIN) continue;
        badpath;
    }
}
