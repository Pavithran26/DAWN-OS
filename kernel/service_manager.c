/* SPDX-License-Identifier: Unlicense */
#include "service_manager.h"
#include "vfs.h"

static struct service_unit service_registry[SERVICE_MAX_UNITS];
static struct ipc_message ipc_queue[SERVICE_MAX_IPC_QUEUE];
static size_t ipc_head = 0;
static size_t ipc_tail = 0;
static size_t ipc_count = 0;

struct event_subscription {
    enum system_event_type type;
    event_listener_t       handler;
    bool                   active;
};

static struct event_subscription event_listeners[SERVICE_MAX_LISTENERS];
static bool service_mgr_initialized = false;

static inline void str_copy(char *dest, const char *src, size_t max_len) {
    size_t i = 0;
    while (src[i] != '\0' && i + 1 < max_len) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static inline bool str_equal(const char *s1, const char *s2) {
    size_t i = 0;
    while (s1[i] != '\0' && s2[i] != '\0') {
        if (s1[i] != s2[i]) return false;
        i++;
    }
    return s1[i] == s2[i];
}

bool service_register(const struct service_unit *unit) {
    if (unit == NULL) return false;

    for (size_t i = 0; i < SERVICE_MAX_UNITS; i++) {
        if (!service_registry[i].active || str_equal(service_registry[i].name, unit->name)) {
            service_registry[i] = *unit;
            service_registry[i].state = SERVICE_STOPPED;
            service_registry[i].pid = 0;
            service_registry[i].active = true;
            return true;
        }
    }
    return false;
}

enum service_state service_get_state(const char *name) {
    if (name == NULL) return SERVICE_STOPPED;

    for (size_t i = 0; i < SERVICE_MAX_UNITS; i++) {
        if (service_registry[i].active && str_equal(service_registry[i].name, name)) {
            return service_registry[i].state;
        }
    }
    return SERVICE_STOPPED;
}

bool service_start(const char *name) {
    if (name == NULL) return false;

    for (size_t i = 0; i < SERVICE_MAX_UNITS; i++) {
        if (service_registry[i].active && str_equal(service_registry[i].name, name)) {
            if (service_registry[i].state == SERVICE_RUNNING) return true;

            /* Check and resolve dependency if specified */
            if (service_registry[i].requires[0] != '\0') {
                enum service_state dep_state = service_get_state(service_registry[i].requires);
                if (dep_state != SERVICE_RUNNING) {
                    if (!service_start(service_registry[i].requires)) {
                        service_registry[i].state = SERVICE_FAILED;
                        return false;
                    }
                }
            }

            service_registry[i].state = SERVICE_STARTING;
            service_registry[i].pid = 200ULL + (uint64_t)i;
            service_registry[i].state = SERVICE_RUNNING;
            return true;
        }
    }
    return false;
}

bool service_stop(const char *name) {
    if (name == NULL) return false;

    for (size_t i = 0; i < SERVICE_MAX_UNITS; i++) {
        if (service_registry[i].active && str_equal(service_registry[i].name, name)) {
            service_registry[i].state = SERVICE_STOPPED;
            service_registry[i].pid = 0;
            return true;
        }
    }
    return false;
}

bool service_start_all(void) {
    for (size_t i = 0; i < SERVICE_MAX_UNITS; i++) {
        if (service_registry[i].active && service_registry[i].state != SERVICE_RUNNING) {
            (void)service_start(service_registry[i].name);
        }
    }
    return true;
}

/* IPC Bus API */
bool ipc_send(uint32_t sender_id, uint32_t receiver_id, uint32_t msg_type, const void *payload, size_t len) {
    if (ipc_count >= SERVICE_MAX_IPC_QUEUE) return false;

    struct ipc_message *m = &ipc_queue[ipc_tail];
    m->sender_id = sender_id;
    m->receiver_id = receiver_id;
    m->msg_type = msg_type;
    m->payload_len = (len < sizeof(m->payload)) ? (uint32_t)len : (uint32_t)sizeof(m->payload);

    const uint8_t *src = (const uint8_t *)payload;
    for (size_t i = 0; i < m->payload_len; i++) {
        m->payload[i] = (src != NULL) ? src[i] : 0;
    }

    ipc_tail = (ipc_tail + 1) % SERVICE_MAX_IPC_QUEUE;
    ipc_count++;
    return true;
}

bool ipc_receive(uint32_t receiver_id, struct ipc_message *out_msg) {
    if (ipc_count == 0 || out_msg == NULL) return false;

    for (size_t i = 0; i < ipc_count; i++) {
        size_t idx = (ipc_head + i) % SERVICE_MAX_IPC_QUEUE;
        if (ipc_queue[idx].receiver_id == receiver_id) {
            *out_msg = ipc_queue[idx];

            /* Compact remaining messages */
            for (size_t j = i; j + 1 < ipc_count; j++) {
                size_t curr = (ipc_head + j) % SERVICE_MAX_IPC_QUEUE;
                size_t next = (ipc_head + j + 1) % SERVICE_MAX_IPC_QUEUE;
                ipc_queue[curr] = ipc_queue[next];
            }

            ipc_count--;
            ipc_tail = (ipc_tail + SERVICE_MAX_IPC_QUEUE - 1) % SERVICE_MAX_IPC_QUEUE;
            return true;
        }
    }

    return false;
}

/* Event System */
bool event_subscribe(enum system_event_type type, event_listener_t handler) {
    if (handler == NULL) return false;

    for (size_t i = 0; i < SERVICE_MAX_LISTENERS; i++) {
        if (!event_listeners[i].active) {
            event_listeners[i].type = type;
            event_listeners[i].handler = handler;
            event_listeners[i].active = true;
            return true;
        }
    }
    return false;
}

bool event_publish(const struct system_event *ev) {
    if (ev == NULL) return false;

    for (size_t i = 0; i < SERVICE_MAX_LISTENERS; i++) {
        if (event_listeners[i].active && event_listeners[i].type == ev->type) {
            event_listeners[i].handler(ev);
        }
    }
    return true;
}

bool service_manager_initialize(void) {
    if (service_mgr_initialized) return true;

    for (size_t i = 0; i < SERVICE_MAX_UNITS; i++) service_registry[i] = (struct service_unit){0};
    for (size_t i = 0; i < SERVICE_MAX_IPC_QUEUE; i++) ipc_queue[i] = (struct ipc_message){0};
    for (size_t i = 0; i < SERVICE_MAX_LISTENERS; i++) event_listeners[i] = (struct event_subscription){0};
    ipc_head = 0;
    ipc_tail = 0;
    ipc_count = 0;

    /* Register core background service daemons */
    struct service_unit devd = {
        .name = "devd",
        .description = "Device event and hotplug manager",
        .binary_path = "/sys/devd",
        .requires = "",
        .restart_policy = RESTART_ALWAYS,
        .active = true
    };
    service_register(&devd);

    struct service_unit netd = {
        .name = "netd",
        .description = "Network configuration daemon",
        .binary_path = "/sys/netd",
        .requires = "devd",
        .restart_policy = RESTART_ALWAYS,
        .active = true
    };
    service_register(&netd);

    struct service_unit audiod = {
        .name = "audiod",
        .description = "Audio service and mixer manager",
        .binary_path = "/sys/audiod",
        .requires = "devd",
        .restart_policy = RESTART_ON_FAILURE,
        .active = true
    };
    service_register(&audiod);

    struct service_unit logd = {
        .name = "logd",
        .description = "Central system logging daemon",
        .binary_path = "/sys/logd",
        .requires = "",
        .restart_policy = RESTART_ALWAYS,
        .active = true
    };
    service_register(&logd);

    service_mgr_initialized = true;
    return true;
}

/* Self-test */
static uint32_t test_event_received_data = 0;
static void test_event_handler(const struct system_event *ev) {
    if (ev != NULL) {
        test_event_received_data = ev->data;
    }
}

bool service_manager_self_test(void) {
    if (!service_manager_initialize()) return false;

    /* Verify service registration */
    if (service_get_state("devd") != SERVICE_STOPPED) return false;
    if (service_get_state("netd") != SERVICE_STOPPED) return false;

    /* Test dependency resolution: starting netd must automatically start devd */
    if (!service_start("netd")) return false;
    if (service_get_state("netd") != SERVICE_RUNNING) return false;
    if (service_get_state("devd") != SERVICE_RUNNING) return false;

    /* Stop service */
    if (!service_stop("netd")) return false;
    if (service_get_state("netd") != SERVICE_STOPPED) return false;

    /* IPC message bus test */
    const char *test_msg = "PING";
    if (!ipc_send(10, 20, 1, test_msg, 4)) return false;

    struct ipc_message received = {0};
    if (!ipc_receive(20, &received)) return false;
    if (received.sender_id != 10 || received.msg_type != 1 || received.payload_len != 4) return false;
    if (received.payload[0] != 'P' || received.payload[1] != 'I' ||
        received.payload[2] != 'N' || received.payload[3] != 'G') return false;

    /* System event pub/sub test */
    test_event_received_data = 0;
    if (!event_subscribe(EVENT_DEVICE_ADDED, test_event_handler)) return false;

    struct system_event ev = {
        .type = EVENT_DEVICE_ADDED,
        .source_id = 1,
        .data = 0xBEEF,
        .description = "PCI device added"
    };
    if (!event_publish(&ev)) return false;
    if (test_event_received_data != 0xBEEF) return false;

    return true;
}
