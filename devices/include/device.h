#ifndef HOMEKIT_DEVICE_H_
#define HOMEKIT_DEVICE_H_

#include <stdbool.h>

#include <hap.h>

typedef struct {
    const char *fixed_name;
    char *name_prefix;
    char *manufacturer;
    char *model;
    char *fw_rev;
    char *hw_rev;
    char *protocol_version;
    hap_cid_t cid;
    hap_identify_routine_t identify;
    int (*add_services)(hap_acc_t *accessory);
    bool uses_custom_display;
    bool uses_custom_buttons;
    void (*init_hardware)(void);
    void (*start_runtime_services)(void);
} homekit_device_t;

const homekit_device_t *device_get_active(void);

#endif /* HOMEKIT_DEVICE_H_ */
