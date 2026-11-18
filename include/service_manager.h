/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_SERVICE_MANAGER_H
#define DAWN_SERVICE_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SERVICE_MAX_UNITS      16U
#define SERVICE_MAX_IPC_QUEUE  32U
#define SERVICE_MAX_LISTENERS  16U

enum service_state {
    SERVICE_STOPPED = 0,
    SERVICE_STARTING = 1,
    SERVICE_RUNNING = 2,
    SERVICE_FAILED = 3
};

enum restart_policy {
    RESTART_NEVER = 0,
    RESTART_ON_FAILURE = 1,
    RESTART_ALWAYS = 2
};

struct service_unit {
    char                name[32];
    char                description[64];
    char                binary_path[64];
    char                requires[32]; /* Name of dependency service */
    enum restart_policy restart_policy;
    enum service_state  state;
    uint64_t            pid;
    bool                active;
};

/* Inter-Process Communication (IPC Message Bus) */
struct ipc_message {
    uint32_t sender_id;
    uint32_t receiver_id;
    uint32_t msg_type;
    uint32_t payload_len;
    uint8_t  payload[64];
};

/* System Event Dispatch System */
enum system_event_type {
    EVENT_DEVICE_ADDED = 0,
    EVENT_DEVICE_REMOVED = 1,
    EVENT_NET_CONNECTED = 2,
    EVENT_POWER_STATE = 3,
    EVENT_TIME_TICK = 4
};

struct system_event {
    enum system_event_type type;
    uint32_t source_id;
    uint32_t data;
    char description[48];
};

typedef void (*event_listener_t)(const struct system_event *ev);

/* Core Service Manager API */
bool service_manager_initialize(void);
bool service_register(const struct service_unit *unit);
bool service_start(const char *name);
bool service_stop(const char *name);
enum service_state service_get_state(const char *name);
bool service_start_all(void);

/* IPC Bus API */
bool ipc_send(uint32_t sender_id, uint32_t receiver_id, uint32_t msg_type, const void *payload, size_t len);
bool ipc_receive(uint32_t receiver_id, struct ipc_message *out_msg);

/* System Event Pub/Sub API */
bool event_publish(const struct system_event *ev);
bool event_subscribe(enum system_event_type type, event_listener_t handler);

/* Self-test */
bool service_manager_self_test(void);

#endif /* DAWN_SERVICE_MANAGER_H */
