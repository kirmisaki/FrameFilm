#ifndef ARK_SIM_SEMPHR_H
#define ARK_SIM_SEMPHR_H
/* The simulator dispatches app/UI work serially on one host thread. */
typedef void *SemaphoreHandle_t;
#define portMAX_DELAY UINT32_MAX
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
static inline int xSemaphoreTake(SemaphoreHandle_t lock, uint32_t ticks)
{ (void)lock; (void)ticks; return 1; }
static inline int xSemaphoreGive(SemaphoreHandle_t lock) { (void)lock; return 1; }
#endif
