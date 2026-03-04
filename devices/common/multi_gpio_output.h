#ifndef MULTI_GPIO_OUTPUT_H_
#define MULTI_GPIO_OUTPUT_H_

#include <stdbool.h>
#include <stddef.h>

#define MULTI_GPIO_OUTPUT_COUNT 3

void multi_gpio_output_init(void);
int multi_gpio_output_set(size_t index, bool value);
bool multi_gpio_output_get(size_t index);

#endif /* MULTI_GPIO_OUTPUT_H_ */
