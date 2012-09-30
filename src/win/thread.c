/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <assert.h>
#include <limits.h>

#include "uv.h"
#include "internal.h"


#define HAVE_SRWLOCK_API() (pTryAcquireSRWLockShared != NULL)

#ifdef _MSC_VER /* msvc */
# define inline __inline
# define NOINLINE __declspec (noinline)
#else  /* gcc */
# define inline inline
# define NOINLINE __attribute__ ((noinline))
#endif


inline static int uv__rwlock_srwlock_init(uv_rwlock_t* rwlock);
inline static void uv__rwlock_srwlock_destroy(uv_rwlock_t* rwlock);
inline static void uv__rwlock_srwlock_rdlock(uv_rwlock_t* rwlock);
inline static int uv__rwlock_srwlock_tryrdlock(uv_rwlock_t* rwlock);
inline static void uv__rwlock_srwlock_rdunlock(uv_rwlock_t* rwlock);
inline static void uv__rwlock_srwlock_wrlock(uv_rwlock_t* rwlock);
inline static int uv__rwlock_srwlock_trywrlock(uv_rwlock_t* rwlock);
inline static void uv__rwlock_srwlock_wrunlock(uv_rwlock_t* rwlock);

inline static int uv__rwlock_fallback_init(uv_rwlock_t* rwlock);
inline static void uv__rwlock_fallback_destroy(uv_rwlock_t* rwlock);
inline static void uv__rwlock_fallback_rdlock(uv_rwlock_t* rwlock);
inline static int uv__rwlock_fallback_tryrdlock(uv_rwlock_t* rwlock);
inline static void uv__rwlock_fallback_rdunlock(uv_rwlock_t* rwlock);
inline static void uv__rwlock_fallback_wrlock(uv_rwlock_t* rwlock);
inline static int uv__rwlock_fallback_trywrlock(uv_rwlock_t* rwlock);
inline static void uv__rwlock_fallback_wrunlock(uv_rwlock_t* rwlock);


static NOINLINE void uv__once_inner(uv_once_t* guard,
    void (*callback)(void)) {
  DWORD result;
  HANDLE existing_event, created_event;

  created_event = CreateEvent(NULL, 1, 0, NULL);
  if (created_event == 0) {
    /* Could fail in a low-memory situation? */
    uv_fatal_error(GetLastError(), "CreateEvent");
  }

  existing_event = InterlockedCompareExchangePointer(&guard->event,
                                                     created_event,
                                                     NULL);

  if (existing_event == NULL) {
    /* We won the race */
    callback();

    result = SetEvent(created_event);
    assert(result);
    guard->ran = 1;

  } else {
    /* We lost the race. Destroy the event we created and wait for the */
    /* existing one to become signaled. */
    CloseHandle(created_event);
    result = WaitForSingleObject(existing_event, INFINITE);
    assert(result == WAIT_OBJECT_0);
  }
}


void uv_once(uv_once_t* guard, void (*callback)(void)) {
  /* Fast case - avoid WaitForSingleObject. */
  if (guard->ran) {
    return;
  }

  uv__once_inner(guard, callback);
}


int uv_thread_join(uv_thread_t *tid) {
  if (WaitForSingleObject(*tid, INFINITE))
    return -1;
  else {
    CloseHandle(*tid);
    *tid = 0;
    return 0;
  }
}


int uv_mutex_init(uv_mutex_t* mutex) {
  *mutex = CreateMutex(NULL, FALSE, NULL);
  return *mutex ? 0 : -1;
}


void uv_mutex_destroy(uv_mutex_t* mutex) {
  if (!CloseHandle(*mutex))
    abort();
}


void uv_mutex_lock(uv_mutex_t* mutex) {
  if (WaitForSingleObject(*mutex, INFINITE) != WAIT_OBJECT_0)
    abort();
}


int uv_mutex_trylock(uv_mutex_t* mutex) {
  DWORD r = WaitForSingleObject(*mutex, 0);

  if (r == WAIT_OBJECT_0)
    return 0;

  if (r == WAIT_TIMEOUT)
    return -1;

  abort();
  return -1; /* Satisfy the compiler. */
}


void uv_mutex_unlock(uv_mutex_t* mutex) {
  if (!ReleaseMutex(*mutex))
    abort();
}


int uv_rwlock_init(uv_rwlock_t* rwlock) {
  if (HAVE_SRWLOCK_API())
    return uv__rwlock_srwlock_init(rwlock);
  else
    return uv__rwlock_fallback_init(rwlock);
}


void uv_rwlock_destroy(uv_rwlock_t* rwlock) {
  if (HAVE_SRWLOCK_API())
    uv__rwlock_srwlock_destroy(rwlock);
  else
    uv__rwlock_fallback_destroy(rwlock);
}


void uv_rwlock_rdlock(uv_rwlock_t* rwlock) {
  if (HAVE_SRWLOCK_API())
    uv__rwlock_srwlock_rdlock(rwlock);
  else
    uv__rwlock_fallback_rdlock(rwlock);
}


int uv_rwlock_tryrdlock(uv_rwlock_t* rwlock) {
  if (HAVE_SRWLOCK_API())
    return uv__rwlock_srwlock_tryrdlock(rwlock);
  else
    return uv__rwlock_fallback_tryrdlock(rwlock);
}


void uv_rwlock_rdunlock(uv_rwlock_t* rwlock) {
  if (HAVE_SRWLOCK_API())
    uv__rwlock_srwlock_rdunlock(rwlock);
  else
    uv__rwlock_fallback_rdunlock(rwlock);
}


void uv_rwlock_wrlock(uv_rwlock_t* rwlock) {
  if (HAVE_SRWLOCK_API())
    uv__rwlock_srwlock_wrlock(rwlock);
  else
    uv__rwlock_fallback_wrlock(rwlock);
}


int uv_rwlock_trywrlock(uv_rwlock_t* rwlock) {
  if (HAVE_SRWLOCK_API())
    return uv__rwlock_srwlock_trywrlock(rwlock);
  else
    return uv__rwlock_fallback_trywrlock(rwlock);
}


void uv_rwlock_wrunlock(uv_rwlock_t* rwlock) {
  if (HAVE_SRWLOCK_API())
    uv__rwlock_srwlock_wrunlock(rwlock);
  else
    uv__rwlock_fallback_wrunlock(rwlock);
}


int uv_sem_init(uv_sem_t* sem, unsigned int value) {
  *sem = CreateSemaphore(NULL, value, INT_MAX, NULL);
  return *sem ? 0 : -1;
}


void uv_sem_destroy(uv_sem_t* sem) {
  if (!CloseHandle(*sem))
    abort();
}


void uv_sem_post(uv_sem_t* sem) {
  if (!ReleaseSemaphore(*sem, 1, NULL))
    abort();
}


void uv_sem_wait(uv_sem_t* sem) {
  if (WaitForSingleObject(*sem, INFINITE) != WAIT_OBJECT_0)
    abort();
}


int uv_sem_trywait(uv_sem_t* sem) {
  DWORD r = WaitForSingleObject(*sem, 0);

  if (r == WAIT_OBJECT_0)
    return 0;

  if (r == WAIT_TIMEOUT)
    return -1;

  abort();
  return -1; /* Satisfy the compiler. */
}


inline static int uv__rwlock_srwlock_init(uv_rwlock_t* rwlock) {
  pInitializeSRWLock(&rwlock->srwlock_);
  return 0;
}


inline static void uv__rwlock_srwlock_destroy(uv_rwlock_t* rwlock) {
  (void) rwlock;
}


inline static void uv__rwlock_srwlock_rdlock(uv_rwlock_t* rwlock) {
  pAcquireSRWLockShared(&rwlock->srwlock_);
}


inline static int uv__rwlock_srwlock_tryrdlock(uv_rwlock_t* rwlock) {
  if (pTryAcquireSRWLockShared(&rwlock->srwlock_))
    return 0;
  else
    return -1;
}


inline static void uv__rwlock_srwlock_rdunlock(uv_rwlock_t* rwlock) {
  pReleaseSRWLockShared(&rwlock->srwlock_);
}


inline static void uv__rwlock_srwlock_wrlock(uv_rwlock_t* rwlock) {
  pAcquireSRWLockExclusive(&rwlock->srwlock_);
}


inline static int uv__rwlock_srwlock_trywrlock(uv_rwlock_t* rwlock) {
  if (pTryAcquireSRWLockExclusive(&rwlock->srwlock_))
    return 0;
  else
    return -1;
}


inline static void uv__rwlock_srwlock_wrunlock(uv_rwlock_t* rwlock) {
  pReleaseSRWLockExclusive(&rwlock->srwlock_);
}


inline static int uv__rwlock_fallback_init(uv_rwlock_t* rwlock) {
  if (uv_mutex_init(&rwlock->fallback_.read_mutex_))
    return -1;

  if (uv_mutex_init(&rwlock->fallback_.write_mutex_)) {
    uv_mutex_destroy(&rwlock->fallback_.read_mutex_);
    return -1;
  }

  rwlock->fallback_.num_readers_ = 0;

  return 0;
}


inline static void uv__rwlock_fallback_destroy(uv_rwlock_t* rwlock) {
  uv_mutex_destroy(&rwlock->fallback_.read_mutex_);
  uv_mutex_destroy(&rwlock->fallback_.write_mutex_);
}


inline static void uv__rwlock_fallback_rdlock(uv_rwlock_t* rwlock) {
  uv_mutex_lock(&rwlock->fallback_.read_mutex_);

  if (++rwlock->fallback_.num_readers_ == 1)
    uv_mutex_lock(&rwlock->fallback_.write_mutex_);

  uv_mutex_unlock(&rwlock->fallback_.read_mutex_);
}


inline static int uv__rwlock_fallback_tryrdlock(uv_rwlock_t* rwlock) {
  int ret;

  ret = -1;

  if (uv_mutex_trylock(&rwlock->fallback_.read_mutex_))
    goto out;

  if (rwlock->fallback_.num_readers_ == 0)
    ret = uv_mutex_trylock(&rwlock->fallback_.write_mutex_);
  else
    ret = 0;

  if (ret == 0)
    rwlock->fallback_.num_readers_++;

  uv_mutex_unlock(&rwlock->fallback_.read_mutex_);

out:
  return ret;
}


inline static void uv__rwlock_fallback_rdunlock(uv_rwlock_t* rwlock) {
  uv_mutex_lock(&rwlock->fallback_.read_mutex_);

  if (--rwlock->fallback_.num_readers_ == 0)
    uv_mutex_unlock(&rwlock->fallback_.write_mutex_);

  uv_mutex_unlock(&rwlock->fallback_.read_mutex_);
}


inline static void uv__rwlock_fallback_wrlock(uv_rwlock_t* rwlock) {
  uv_mutex_lock(&rwlock->fallback_.write_mutex_);
}


inline static int uv__rwlock_fallback_trywrlock(uv_rwlock_t* rwlock) {
  return uv_mutex_trylock(&rwlock->fallback_.write_mutex_);
}


inline static void uv__rwlock_fallback_wrunlock(uv_rwlock_t* rwlock) {
  uv_mutex_unlock(&rwlock->fallback_.write_mutex_);
}


/* This implementation is based on the SignalObjectAndWait solution
 * (section 3.4) at
 * http://www.cs.wustl.edu/~schmidt/win32-cv-1.html
 * Note: uv_mutex_t must be defined as HANDLE, not CRITICAL_SECTION.
 */

void uv_cond_init(uv_cond_t* cond) {
  cond->waiters_count = 0;
  cond->was_broadcast = 0;
  cond->sema = CreateSemaphore(NULL,       /* no security */
                               0,          /* initially 0 */
                               0x7fffffff, /* max count   */
                               NULL);      /* unnamed     */
  InitializeCriticalSection(&cond->waiters_count_lock);
  cond->waiters_done = CreateEvent(NULL,  /* no security            */
                                   FALSE, /* auto-reset             */
                                   FALSE, /* non-signaled initially */
                                   NULL); /* unnamed                */
}


void uv_cond_destroy(uv_cond_t* cond) {
  if (!CloseHandle(cond->waiters_done))
    abort();
  DeleteCriticalSection(&cond->waiters_count_lock);
  if (!CloseHandle(cond->sema))
    abort();
}


void uv_cond_signal(uv_cond_t* cond) {
  int have_waiters;

  EnterCriticalSection(&cond->waiters_count_lock);
  have_waiters = cond->waiters_count > 0;
  LeaveCriticalSection(&cond->waiters_count_lock);

  /* If there aren't any waiters, then this is a no-op. */
  if (have_waiters)
    ReleaseSemaphore(cond->sema, 1, 0);
}

void uv_cond_broadcast(uv_cond_t* cond) {
  int have_waiters;

  /* This is needed to ensure that <waiters_count> and <was_broadcast> are
   * consistent relative to each other.
   */
  EnterCriticalSection(&cond->waiters_count_lock);
  have_waiters = 0;

  if (cond->waiters_count > 0) {
    /* We are broadcasting, even if there is just one waiter...
     * Record that we are broadcasting, which helps optimize
     * <pthread_cond_wait> for the non-broadcast case.
     */
    cond->was_broadcast = 1;
    have_waiters = 1;
  }

  if (have_waiters) {
    /* Wake up all the waiters atomically. */
    ReleaseSemaphore(cond->sema, cond->waiters_count, 0);

    LeaveCriticalSection(&cond->waiters_count_lock);

    /* Wait for all the awakened threads to acquire the counting
     * semaphore.
     */
    WaitForSingleObject(cond->waiters_done, INFINITE);
    /* This assignment is okay, even without the <waiters_count_lock> held
     * because no other waiter threads can wake up to access it.
     */
    cond->was_broadcast = 0;
  } else
    LeaveCriticalSection(&cond->waiters_count_lock);
}


static int uv_cond_wait_helper(uv_cond_t* cond, uv_mutex_t* mutex,
    DWORD dwMilliseconds) {
  DWORD result;
  int last_waiter;

  /* Avoid race conditions. */
  EnterCriticalSection(&cond->waiters_count_lock);
  cond->waiters_count++;
  LeaveCriticalSection(&cond->waiters_count_lock);

  /* This call atomically releases the mutex and waits on the
   * semaphore until <pthread_cond_signal> or <pthread_cond_broadcast>
   * are called by another thread.
   */
  result = SignalObjectAndWait(*mutex, cond->sema, dwMilliseconds, FALSE);

  /* Reacquire lock to avoid race conditions. */
  EnterCriticalSection(&cond->waiters_count_lock);

  /* We're no longer waiting... */
  cond->waiters_count--;

  /* Check to see if we're the last waiter after <pthread_cond_broadcast>. */
  last_waiter = cond->was_broadcast && cond->waiters_count == 0;

  LeaveCriticalSection(&cond->waiters_count_lock);

  /* If we're the last waiter thread during this particular broadcast
   * then let all the other threads proceed.
   */
  if (last_waiter) {
    /* This call atomically signals the <waiters_done> event and waits until
     * it can acquire the <mutex>.  This is required to ensure
     * fairness.
     */
    SignalObjectAndWait(cond->waiters_done, *mutex, INFINITE, FALSE);
  } else {
    /* Always regain the external mutex since that's the guarantee we
     * give to our callers.
     */
    WaitForSingleObject(*mutex, INFINITE);
  }

  /* See http://msdn.microsoft.com/en-us/library/windows/desktop/ms686293(v=vs.85).aspx
   * for the return code of SignalObjectAndWait.
   * Note: I thinks it's correct to return UV_OK for WAIT_OBJECT_0.
   * I am not sure what to return for WAIT_ABONDONED, WAIT_IO_COMPLETION,
   * I choosed UV_OK at this time.
   */
  switch (result) {
  case WAIT_FAILED:  return UV_UNKNOWN;
  case WAIT_TIMEOUT: return UV_ETIMEDOUT;
  default:           return UV_OK;
  }
}


void uv_cond_wait(uv_cond_t* cond, uv_mutex_t* mutex) {
  int r;

  r = uv_cond_wait_helper(cond, mutex, INFINITE);
  if (r != UV_OK)
    abort();
}


int uv_cond_timedwait(uv_cond_t* cond, uv_mutex_t* mutex,
    uint64_t timeout) {
  return uv_cond_wait_helper(cond, mutex, (DWORD)(timeout / 1e6));
}
