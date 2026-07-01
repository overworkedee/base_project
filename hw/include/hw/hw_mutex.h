#ifndef HW_MUTEX_H
#define HW_MUTEX_H

#include <pthread.h>
#include "hw/hw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef pthread_mutex_t hw_mutex_t;

/**
 * @brief Initialize a mutex.
 * @param mutex  Pointer to the mutex.
 * @return HW_OK on success, or HW_ERR_MUTEX on failure.
 */
hw_err_t hw_mutex_init(hw_mutex_t* mutex);

/**
 * @brief Lock a mutex.
 * @param mutex  Pointer to the mutex.
 * @return HW_OK on success, or HW_ERR_MUTEX on failure.
 */
hw_err_t hw_mutex_lock(hw_mutex_t* mutex);

/**
 * @brief Unlock a mutex.
 * @param mutex  Pointer to the mutex.
 * @return HW_OK on success, or HW_ERR_MUTEX on failure.
 */
hw_err_t hw_mutex_unlock(hw_mutex_t* mutex);

/**
 * @brief Destroy a mutex.
 * @param mutex  Pointer to the mutex.
 * @return HW_OK on success, or HW_ERR_MUTEX on failure.
 */
hw_err_t hw_mutex_destroy(hw_mutex_t* mutex);

#ifdef __cplusplus
}
#endif

#endif /* HW_MUTEX_H */
