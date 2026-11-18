/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_NVME_H
#define DAWN_NVME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "block_dev.h"
#include "pci.h"

#define NVME_MAX_NAMESPACES 4U

/* NVMe Admin Opcodes */
#define NVME_ADMIN_CMD_DELETE_SQ   0x00U
#define NVME_ADMIN_CMD_CREATE_SQ   0x01U
#define NVME_ADMIN_CMD_DELETE_CQ   0x04U
#define NVME_ADMIN_CMD_CREATE_CQ   0x05U
#define NVME_ADMIN_CMD_IDENTIFY    0x06U
#define NVME_ADMIN_CMD_SET_FEATURES 0x09U

/* NVMe NVM I/O Opcodes */
#define NVME_IO_CMD_FLUSH 0x00U
#define NVME_IO_CMD_WRITE 0x01U
#define NVME_IO_CMD_READ  0x02U

struct nvme_controller_registers {
    volatile uint64_t cap;      /* Controller Capabilities */
    volatile uint32_t vs;       /* Version */
    volatile uint32_t intms;    /* Interrupt Mask Set */
    volatile uint32_t intmc;    /* Interrupt Mask Clear */
    volatile uint32_t cc;       /* Controller Configuration */
    volatile uint32_t reserved0;
    volatile uint32_t csts;     /* Controller Status */
    volatile uint32_t nssr;     /* NVM Subsystem Reset */
    volatile uint32_t aqa;      /* Admin Queue Attributes */
    volatile uint64_t asq;      /* Admin Submission Queue Base */
    volatile uint64_t acq;      /* Admin Completion Queue Base */
} __attribute__((packed));

struct nvme_sqe {
    uint8_t  opcode;
    uint8_t  flags;
    uint16_t cid;
    uint32_t nsid;
    uint64_t reserved0;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} __attribute__((packed));

_Static_assert(sizeof(struct nvme_sqe) == 64, "NVMe SQE must be exactly 64 bytes");

struct nvme_cqe {
    uint32_t cdw0;
    uint32_t reserved;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t cid;
    uint16_t status;
} __attribute__((packed));

_Static_assert(sizeof(struct nvme_cqe) == 16, "NVMe CQE must be exactly 16 bytes");

struct nvme_namespace {
    uint32_t nsid;
    uint64_t size_blocks;
    uint32_t block_size;
    bool active;
    struct block_device block_dev;
};

struct nvme_controller {
    struct nvme_controller_registers *regs;
    uint32_t doorbell_stride;
    char model[41];
    char serial[21];
    char firmware[9];
    uint32_t max_queue_entries;
    struct nvme_namespace namespaces[NVME_MAX_NAMESPACES];
    size_t namespace_count;
    bool active;
};

bool nvme_initialize(void);
size_t nvme_get_controller_count(void);
struct nvme_controller *nvme_get_controller(size_t index);
int nvme_read(struct nvme_namespace *ns, uint64_t lba, uint32_t count, void *buffer);
int nvme_write(struct nvme_namespace *ns, uint64_t lba, uint32_t count, const void *buffer);

bool nvme_self_test(void);

#endif /* DAWN_NVME_H */
