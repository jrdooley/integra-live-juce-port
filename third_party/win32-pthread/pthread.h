/*
    Minimal pthreads for Windows - only what Pd 0.43's core uses
    (m_sched.c: the scheduler lock; d_soundfile.c: readsf~/writesf~ disk threads).

    Part of the Integra Live JUCE port. Lets libpd build with MSVC without a
    pthreads-win32 dependency. Maps onto native Win32 primitives:

        pthread_mutex_t  -> SRWLOCK (exclusive; non-recursive like a default pthread mutex)
        pthread_cond_t   -> CONDITION_VARIABLE
        pthread_t        -> thread HANDLE from _beginthreadex

    Only included on Windows (third_party/CMakeLists.txt adds this directory to the
    include path for WIN32 builds). Public domain / same terms as the Integra port.
*/

#ifndef INTEGRA_WIN32_PTHREAD_SHIM_H
#define INTEGRA_WIN32_PTHREAD_SHIM_H

#ifndef _WIN32
#error "win32-pthread/pthread.h is only for Windows builds"
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <process.h>
#include <errno.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef SRWLOCK pthread_mutex_t;
typedef CONDITION_VARIABLE pthread_cond_t;
typedef HANDLE pthread_t;
typedef void pthread_mutexattr_t;
typedef void pthread_condattr_t;
typedef void pthread_attr_t;

#define PTHREAD_MUTEX_INITIALIZER SRWLOCK_INIT
#define PTHREAD_COND_INITIALIZER CONDITION_VARIABLE_INIT

static __inline int pthread_mutex_init (pthread_mutex_t *m, const pthread_mutexattr_t *a)  { (void) a; InitializeSRWLock (m); return 0; }
static __inline int pthread_mutex_destroy (pthread_mutex_t *m)                             { (void) m; return 0; }
static __inline int pthread_mutex_lock (pthread_mutex_t *m)                                { AcquireSRWLockExclusive (m); return 0; }
static __inline int pthread_mutex_unlock (pthread_mutex_t *m)                              { ReleaseSRWLockExclusive (m); return 0; }
static __inline int pthread_mutex_trylock (pthread_mutex_t *m)                             { return TryAcquireSRWLockExclusive (m) ? 0 : EBUSY; }

static __inline int pthread_cond_init (pthread_cond_t *c, const pthread_condattr_t *a)     { (void) a; InitializeConditionVariable (c); return 0; }
static __inline int pthread_cond_destroy (pthread_cond_t *c)                               { (void) c; return 0; }
static __inline int pthread_cond_signal (pthread_cond_t *c)                                { WakeConditionVariable (c); return 0; }
static __inline int pthread_cond_broadcast (pthread_cond_t *c)                             { WakeAllConditionVariable (c); return 0; }
static __inline int pthread_cond_wait (pthread_cond_t *c, pthread_mutex_t *m)
{
    return SleepConditionVariableSRW (c, m, INFINITE, 0) ? 0 : EINVAL;
}

typedef struct
{
    void *(*start) (void *);
    void *arg;
} integra_pthread_start_info;

static unsigned __stdcall integra_pthread_trampoline (void *p)
{
    integra_pthread_start_info info = *(integra_pthread_start_info *) p;
    free (p);
    info.start (info.arg);
    return 0;
}

static __inline int pthread_create (pthread_t *t, const pthread_attr_t *a, void *(*start) (void *), void *arg)
{
    integra_pthread_start_info *info = (integra_pthread_start_info *) malloc (sizeof (integra_pthread_start_info));
    (void) a;
    if (! info)
        return ENOMEM;
    info->start = start;
    info->arg = arg;
    *t = (HANDLE) _beginthreadex (NULL, 0, integra_pthread_trampoline, info, 0, NULL);
    if (*t == 0)
    {
        free (info);
        return EAGAIN;
    }
    return 0;
}

static __inline int pthread_join (pthread_t t, void **result)
{
    if (WaitForSingleObject (t, INFINITE) != WAIT_OBJECT_0)
        return EINVAL;
    CloseHandle (t);
    if (result)
        *result = NULL;   /* Pd ignores the thread's return value */
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* INTEGRA_WIN32_PTHREAD_SHIM_H */
