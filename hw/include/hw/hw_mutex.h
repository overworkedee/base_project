#ifndef HW_MUTEX_H
#define HW_MUTEX_H

#include <pthread.h>
#include "hw/hw_error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef pthread_mutex_t hw_mutex_t;

hw_err_t hw_mutex_init(hw_mutex_t* mutex);
hw_err_t hw_mutex_lock(hw_mutex_t* mutex);
hw_err_t hw_mutex_unlock(hw_mutex_t* mutex);
hw_err_t hw_mutex_destroy(hw_mutex_t* mutex);

#ifdef __cplusplus
}
#endif

#endif /* HW_MUTEX_H */
