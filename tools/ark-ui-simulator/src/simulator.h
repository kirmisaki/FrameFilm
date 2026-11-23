#ifndef ARK_SIMULATOR_H
#define ARK_SIMULATOR_H
#include <stdint.h>
#include "app_interface.h"
#include "ui_ops.h"

void simulator_show(const app_ui_ops_t *ops, const app_entry_t *entry);
void simulator_process_requests(void);
const app_entry_t *simulator_current_entry(void);
const app_ui_ops_t *simulator_current_ops(void);
int simulator_take_boot_done(void);

typedef struct {
    int active;
    int x, y, w, h;
} simulator_color_card_t;

simulator_color_card_t simulator_color_card(void);

#endif
