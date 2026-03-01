#ifndef GPIO_OUTPUT_H_
#define GPIO_OUTPUT_H_

#include <stdbool.h>

void gpio_output_init(void);
int gpio_output_set_on(bool value);
bool gpio_output_get_on(void);

#endif /* GPIO_OUTPUT_H_ */
