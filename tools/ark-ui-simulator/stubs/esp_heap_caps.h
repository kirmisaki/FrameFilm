#ifndef ARK_SIM_ESP_HEAP_CAPS_H
#define ARK_SIM_ESP_HEAP_CAPS_H
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
#define MALLOC_CAP_INTERNAL 0
#ifdef ARK_UI_SIMULATOR_REAL_DISPLAY
extern int simulator_heap_fail_after;
#endif
static inline void *heap_caps_malloc(size_t size, int caps)
{
    (void)caps;
#ifdef ARK_UI_SIMULATOR_REAL_DISPLAY
    if(simulator_heap_fail_after == 0) return NULL;
    if(simulator_heap_fail_after > 0) simulator_heap_fail_after--;
#endif
    return malloc(size);
}
static inline void heap_caps_free(void *ptr) { free(ptr); }
static inline size_t heap_caps_get_free_size(int caps) { (void)caps; return 0; }
static inline size_t heap_caps_get_largest_free_block(int caps) { (void)caps; return 0; }
#endif
