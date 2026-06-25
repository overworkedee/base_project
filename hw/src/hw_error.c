#include "hw/hw_error.h"

const char* hw_err_str(hw_err_t err)
{
    switch (err) {
    case HW_OK:                return "OK";
    case HW_ERR_BUS_OPEN:      return "Bus open failed";
    case HW_ERR_BUS_TRANSFER:  return "Bus transfer failed";
    case HW_ERR_DEV_ADDR:      return "Device address invalid";
    case HW_ERR_DEV_NOT_FOUND: return "Device not found (no ACK)";
    case HW_ERR_MUTEX:    return "Mutex operation failed";
    case HW_ERR_PARAM:         return "Invalid parameter";
    default:                   return "Unknown error";
    }
}
