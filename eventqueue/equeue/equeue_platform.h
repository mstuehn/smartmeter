/*
 * System specific implementation
 *
 * Copyright (c) 2016 Christopher Haster
 * Distributed under the MIT license
 */
#ifndef EQUEUE_PLATFORM_H
#define EQUEUE_PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

// Currently supported platforms
//
// Uncomment to select a supported platform or reimplement this file
// for a specific target.
//#define EQUEUE_PLATFORM_POSIX
//#define EQUEUE_PLATFORM_WINDOWS
//#define EQUEUE_PLATFORM_MBED
//#define EQUEUE_PLATFORM_FREERTOS

// Try to infer a platform if none was manually selected
#if !defined(EQUEUE_PLATFORM_POSIX)     \
 && !defined(EQUEUE_PLATFORM_WINDOWS)   \
 && !defined(EQUEUE_PLATFORM_MBED)      \
 && !defined(EQUEUE_PLATFORM_FREERTOS)
#if defined(__unix__) | defined(__APPLE__)
#define EQUEUE_PLATFORM_POSIX
#elif defined(_WIN32)
#define EQUEUE_PLATFORM_WINDOWS
#elif defined(__MBED__)
#define EQUEUE_PLATFORM_MBED
#else
#warning "Unknown platform! Please update equeue_platform.h"
#endif
#endif

// Platform includes
#if defined(EQUEUE_PLATFORM_POSIX)
#include <pthread.h>
#elif defined(EQUEUE_PLATFORM_WINDOWS)
#include <windows.h>
#elif defined(EQUEUE_PLATFORM_FREERTOS)
#include "FreeRTOS.h"
#include "semphr.h"
#endif


// Platform millisecond counter
//
// Return a tick that represents the number of milliseconds that have passed
// since an arbitrary point in time. The granularity does not need to be at
// the millisecond level, however the accuracy of the equeue library is
// limited by the accuracy of this tick.
//
// Must intentionally overflow to 0 after 2^32-1
unsigned equeue_tick(void);


// Platform mutex type
//
// The equeue library requires at minimum a non-recursive mutex that is
// safe in interrupt contexts. The mutex section is help for a bounded
// amount of time, so simply disabling interrupts is acceptable
//
// If irq safety is not required, a regular blocking mutex can be used.
#if defined(EQUEUE_PLATFORM_POSIX)
typedef pthread_mutex_t equeue_mutex_t;
#elif defined(EQUEUE_PLATFORM_WINDOWS)
typedef CRITICAL_SECTION equeue_mutex_t;
#elif defined(EQUEUE_PLATFORM_MBED)
typedef unsigned equeue_mutex_t;
#elif defined(EQUEUE_PLATFORM_FREERTOS)
typedef UBaseType_t equeue_mutex_t;
#endif

// Platform mutex operations
//
// The equeue_mutex_create and equeue_mutex_destroy manage the lifetime
// of the mutex. On error, equeue_mutex_create should return a negative
// error code.
//
// The equeue_mutex_lock and equeue_mutex_unlock lock and unlock the
// underlying mutex.
int equeue_mutex_create(equeue_mutex_t *mutex);
void equeue_mutex_destroy(equeue_mutex_t *mutex);
void equeue_mutex_lock(equeue_mutex_t *mutex);
void equeue_mutex_unlock(equeue_mutex_t *mutex);


// Platform semaphore type
//
// The equeue library requires a binary semaphore type that can be safely
// signaled from interrupt contexts and from inside a equeue_mutex section.
//
// The equeue_signal_wait is relied upon by the equeue library to sleep the
// processor between events. Spurious wakeups have no negative-effects.
//
// A counting semaphore will also work, however may cause the event queue
// dispatch loop to run unnecessarily. For that matter, equeue_signal_wait
// may even be implemented as a single return statement.
#if defined(EQUEUE_PLATFORM_POSIX)
typedef struct equeue_sema {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool signal;
} equeue_sema_t;
#elif defined(EQUEUE_PLATFORM_WINDOWS)
typedef HANDLE equeue_sema_t;
#elif defined(EQUEUE_PLATFORM_MBED) && defined(MBED_CONF_RTOS_PRESENT)
typedef unsigned equeue_sema_t[8];
#elif defined(EQUEUE_PLATFORM_MBED)
typedef volatile int equeue_sema_t;
#elif defined(EQUEUE_PLATFORM_FREERTOS)
typedef struct equeue_sema {
    SemaphoreHandle_t handle;
    StaticSemaphore_t buffer;
} equeue_sema_t;
#endif

// Platform semaphore operations
//
// The equeue_sema_create and equeue_sema_destroy manage the lifetime
// of the semaphore. On error, equeue_sema_create should return a negative
// error code.
//
// The equeue_sema_signal marks a semaphore as signalled such that the next
// equeue_sema_wait will return true.
//
// The equeue_sema_wait waits for a semaphore to be signalled or returns
// immediately if equeue_sema_signal had been called since the last
// equeue_sema_wait. The equeue_sema_wait returns true if it detected that
// equeue_sema_signal had been called.
int equeue_sema_create(equeue_sema_t *sema);
void equeue_sema_destroy(equeue_sema_t *sema);
void equeue_sema_signal(equeue_sema_t *sema);
bool equeue_sema_wait(equeue_sema_t *sema, int ms);


#ifdef __cplusplus
}
#endif

#endif
