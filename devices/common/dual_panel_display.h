#ifndef DUAL_PANEL_DISPLAY_H_
#define DUAL_PANEL_DISPLAY_H_

#include <stdbool.h>
#include <stddef.h>

void dual_panel_display_init(void);
void dual_panel_display_start(void);
void dual_panel_display_set_light(size_t index, bool is_on);
void dual_panel_display_set_button(size_t index, bool is_pressed);
void dual_panel_display_request_poem_refresh(void);
void dual_panel_display_request_right_refresh(void);

#endif /* DUAL_PANEL_DISPLAY_H_ */
