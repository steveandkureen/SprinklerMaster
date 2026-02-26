#include "history.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static history_event_t events[HISTORY_MAX_EVENTS];
static int write_index = 0;
static int event_count = 0;

void history_log(uint8_t zone, history_trigger_t trigger, int16_t duration_sec) {
    events[write_index].timestamp = (uint32_t)time(NULL);
    events[write_index].zone = zone;
    events[write_index].trigger = (uint8_t)trigger;
    events[write_index].duration_sec = duration_sec;

    write_index = (write_index + 1) % HISTORY_MAX_EVENTS;
    if (event_count < HISTORY_MAX_EVENTS) {
        event_count++;
    }
}

int history_get_count(void) {
    return event_count;
}

static const char *trigger_str(uint8_t trigger) {
    switch (trigger) {
    case HIST_MANUAL:      return "manual";
    case HIST_SCHEDULE:    return "schedule";
    case HIST_PROGRAM:     return "program";
    case HIST_FREEZE_SKIP: return "freeze_skip";
    default:               return "unknown";
    }
}

int history_get_json(char *buf, size_t buflen) {
    char *p = buf;
    int remaining = (int)buflen;
    int n;

    n = snprintf(p, remaining, "[");
    p += n; remaining -= n;

    // Iterate newest-first
    for (int i = 0; i < event_count && remaining > 80; i++) {
        int idx = (write_index - 1 - i + HISTORY_MAX_EVENTS) % HISTORY_MAX_EVENTS;
        const history_event_t *e = &events[idx];

        n = snprintf(p, remaining, "%s{\"z\":%d,\"t\":\"%s\",\"d\":%d,\"ts\":%lu}",
                     i > 0 ? "," : "",
                     e->zone, trigger_str(e->trigger),
                     e->duration_sec, (unsigned long)e->timestamp);
        p += n; remaining -= n;
    }

    n = snprintf(p, remaining, "]");
    p += n;

    return (int)(p - buf);
}
