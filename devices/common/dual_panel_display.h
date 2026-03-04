#ifndef DUAL_PANEL_DISPLAY_H_
#define DUAL_PANEL_DISPLAY_H_

#include <stdbool.h>
#include <stddef.h>

void dual_panel_display_init(void);
void dual_panel_display_start(void);
void dual_panel_display_set_light(size_t index, bool is_on);

#endif /* DUAL_PANEL_DISPLAY_H_ */
