#include "device.h"

#include "light_device.h"
#include "outlet_device.h"

const homekit_device_t *device_get_active(void)
{
#if HOMEKIT_DEVICE_TYPE_LIGHT
    return light_device_get();
#else
    return outlet_device_get();
#endif
}
