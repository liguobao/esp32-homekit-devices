#ifndef DISPLAY_SUPPORT_H_
#define DISPLAY_SUPPORT_H_

#include <stdbool.h>

void display_support_init(void);
void display_support_show_boot(const char *accessory_name,
        const char *model, const char *setup_code);
void display_support_show_power(bool is_on);

#endif /* DISPLAY_SUPPORT_H_ */
