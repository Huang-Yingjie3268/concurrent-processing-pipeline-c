/* Test-only GNU ld wrappers; never linked into the normal executable. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>

static int created, joined, sems, destroyed_sems, mutexes, destroyed_mutexes;
static int create_calls, sem_calls, mutex_calls, calloc_calls;

static int fail_at(const char *name, int call)
{
    const char *value = getenv(name);
    return value && atoi(value) == call;
}

int __real_pthread_create(pthread_t *, const pthread_attr_t *,
                          void *(*)(void *), void *);
int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                          void *(*routine)(void *), void *arg)
{
    if (fail_at("FAIL_CREATE", ++create_calls)) return EAGAIN;
    int result = __real_pthread_create(thread, attr, routine, arg);
    if (result == 0) ++created;
    return result;
}

int __real_pthread_join(pthread_t, void **);
int __wrap_pthread_join(pthread_t thread, void **value)
{
    int result = __real_pthread_join(thread, value);
    if (result == 0) ++joined;
    return result;
}

int __real_sem_init(sem_t *, int, unsigned int);
int __wrap_sem_init(sem_t *sem, int shared, unsigned int value)
{
    if (fail_at("FAIL_SEM_INIT", ++sem_calls)) { errno = ENOSPC; return -1; }
    int result = __real_sem_init(sem, shared, value);
    if (result == 0) ++sems;
    return result;
}

int __real_sem_destroy(sem_t *);
int __wrap_sem_destroy(sem_t *sem)
{
    int result = __real_sem_destroy(sem);
    if (result == 0) ++destroyed_sems;
    return result;
}

int __real_pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
int __wrap_pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    if (fail_at("FAIL_MUTEX_INIT", ++mutex_calls)) return ENOMEM;
    int result = __real_pthread_mutex_init(mutex, attr);
    if (result == 0) ++mutexes;
    return result;
}

int __real_pthread_mutex_destroy(pthread_mutex_t *);
int __wrap_pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    int result = __real_pthread_mutex_destroy(mutex);
    if (result == 0) ++destroyed_mutexes;
    return result;
}

void *__real_calloc(size_t, size_t);
void *__wrap_calloc(size_t count, size_t size)
{
    if (fail_at("FAIL_CALLOC", ++calloc_calls)) { errno = ENOMEM; return NULL; }
    return __real_calloc(count, size);
}

int __real_sem_wait(sem_t *);
int __wrap_sem_wait(sem_t *sem)
{
    static _Thread_local int interrupted;
    if (getenv("INTERRUPT_WAIT") && !interrupted) {
        interrupted = 1;
        errno = EINTR;
        return -1;
    }
    return __real_sem_wait(sem);
}

int __real_rand(void);
int __wrap_rand(void)
{
    return getenv("FIXED_RAW") ? 37 : __real_rand();
}

__attribute__((destructor)) static void accounting(void)
{
    if (!getenv("CHECK_ACCOUNTING")) return;
    int balanced = created == joined && sems == destroyed_sems &&
                   mutexes == destroyed_mutexes;
    fprintf(stderr, "Fault accounting: %s (threads %d/%d, semaphores %d/%d, "
            "mutexes %d/%d)\n", balanced ? "balanced" : "UNBALANCED",
            created, joined, sems, destroyed_sems, mutexes, destroyed_mutexes);
}
