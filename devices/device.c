#include "device.h"

#include "dashboard_device.h"
#include "epaper_device.h"
#include "light_device.h"
#include "outlet_device.h"

const homekit_device_t *device_get_active(void)
{
#if HOMEKIT_DEVICE_TYPE_LIGHT
    return light_device_get();
#elif HOMEKIT_DEVICE_TYPE_DASHBOARD
    return dashboard_device_get();
#elif HOMEKIT_DEVICE_TYPE_EPAPER
    return epaper_device_get();
#else
    return outlet_device_get();
#endif
}
