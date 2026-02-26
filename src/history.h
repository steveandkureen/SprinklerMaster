#ifndef HISTORY_H
#define HISTORY_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    HIST_MANUAL = 0,
    HIST_SCHEDULE,
    HIST_PROGRAM,
    HIST_FREEZE_SKIP
} history_trigger_t;

typedef struct {
    uint32_t timestamp;      // time_t from time(NULL)
    uint8_t  zone;           // zone ID (1-8)
    uint8_t  trigger;        // history_trigger_t
    int16_t  duration_sec;   // actual run duration (0 for freeze skip)
} history_event_t;

#define HISTORY_MAX_EVENTS 50

void history_log(uint8_t zone, history_trigger_t trigger, int16_t duration_sec);
int  history_get_count(void);
int  history_get_json(char *buf, size_t buflen);

#endif // HISTORY_H
