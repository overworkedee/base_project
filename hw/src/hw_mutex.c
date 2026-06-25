#include "hw/hw_mutex.h"

hw_err_t hw_mutex_init(hw_mutex_t* mutex)
{
    if (!mutex) return HW_ERR_PARAM;
    int ret = pthread_mutex_init(mutex, NULL);
    return (ret == 0) ? HW_OK : HW_ERR_MUTEX;
}

hw_err_t hw_mutex_lock(hw_mutex_t* mutex)
{
    if (!mutex) return HW_ERR_PARAM;
    int ret = pthread_mutex_lock(mutex);
    return (ret == 0) ? HW_OK : HW_ERR_MUTEX;
}

hw_err_t hw_mutex_unlock(hw_mutex_t* mutex)
{
    if (!mutex) return HW_ERR_PARAM;
    int ret = pthread_mutex_unlock(mutex);
    return (ret == 0) ? HW_OK : HW_ERR_MUTEX;
}

hw_err_t hw_mutex_destroy(hw_mutex_t* mutex)
{
    if (!mutex) return HW_ERR_PARAM;
    int ret = pthread_mutex_destroy(mutex);
    return (ret == 0) ? HW_OK : HW_ERR_MUTEX;
}
