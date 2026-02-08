#ifndef NETWORK_H
#define NETWORK_H

#include <stdbool.h>
#include <stdint.h>

// Initialize network (WiFi, NTP, HTTP server)
bool network_init(void);

// Check WiFi connection and reconnect if needed
// Call periodically from main loop (~30 seconds)
void network_check_wifi(void);

// Inter-core command protocol (Core 1 → Core 0)
// Bits 31-24: command, 23-16: zone_id (if applicable), 15-0: duration (if applicable)
//
// NOTE: Cannot use the hardware multicore FIFO because flash_safe_execute_core_init()
// installs a FIFO IRQ handler that consumes all non-lockout data. Use a shared-memory
// SPSC ring buffer instead.
#define NET_CMD_CONFIG_RELOAD  0x01000000
#define NET_CMD_ZONE_ON        0x02000000
#define NET_CMD_ZONE_OFF       0x03000000
#define NET_CMD_MASK           0xFF000000
#define NET_CMD_ZONE_ID(cmd)   (((cmd) >> 16) & 0xFF)
#define NET_CMD_DURATION(cmd)  ((cmd) & 0xFFFF)
#define NET_MAKE_ZONE_ON(id, dur) (NET_CMD_ZONE_ON | ((uint32_t)(id) << 16) | ((uint32_t)(dur) & 0xFFFF))

// Lock-free SPSC command queue (single producer Core 1, single consumer Core 0)
#define NET_CMD_QUEUE_SIZE 8

typedef struct {
    volatile uint32_t cmds[NET_CMD_QUEUE_SIZE];
    volatile uint8_t head;  // Next write position (Core 1 only)
    volatile uint8_t tail;  // Next read position (Core 0 only)
} net_cmd_queue_t;

extern net_cmd_queue_t g_cmd_queue;

// Push a command (called from Core 1 only). Drops command if queue is full.
static inline void net_cmd_push(uint32_t cmd) {
    uint8_t next = (g_cmd_queue.head + 1) % NET_CMD_QUEUE_SIZE;
    if (next != g_cmd_queue.tail) {
        g_cmd_queue.cmds[g_cmd_queue.head] = cmd;
        g_cmd_queue.head = next;
    }
}

// Pop a command (called from Core 0 only). Returns false if queue is empty.
static inline bool net_cmd_pop(uint32_t *cmd) {
    if (g_cmd_queue.head == g_cmd_queue.tail) {
        return false;
    }
    *cmd = g_cmd_queue.cmds[g_cmd_queue.tail];
    g_cmd_queue.tail = (g_cmd_queue.tail + 1) % NET_CMD_QUEUE_SIZE;
    return true;
}

#endif // NETWORK_H
