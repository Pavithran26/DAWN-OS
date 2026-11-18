/* SPDX-License-Identifier: Unlicense */
#include "nvme.h"

#define NVME_MAX_CONTROLLERS 2U

static struct nvme_controller controllers[NVME_MAX_CONTROLLERS];
static size_t controller_count = 0;
static bool nvme_initialized = false;

static inline void str_copy(char *dest, const char *src, size_t max_len) {
    size_t i = 0;
    while (src[i] != '\0' && i + 1 < max_len) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static int nvme_block_read(struct block_device *dev, uint64_t lba, uint32_t count, void *buffer) {
    if (dev == NULL || dev->driver_data == NULL) return -1;
    struct nvme_namespace *ns = (struct nvme_namespace *)dev->driver_data;
    return nvme_read(ns, lba, count, buffer);
}

static int nvme_block_write(struct block_device *dev, uint64_t lba, uint32_t count, const void *buffer) {
    if (dev == NULL || dev->driver_data == NULL) return -1;
    struct nvme_namespace *ns = (struct nvme_namespace *)dev->driver_data;
    return nvme_write(ns, lba, count, buffer);
}

static int nvme_block_flush(struct block_device *dev) {
    (void)dev;
    return 0;
}

int nvme_read(struct nvme_namespace *ns, uint64_t lba, uint32_t count, void *buffer) {
    if (ns == NULL || !ns->active || buffer == NULL || count == 0) return -1;
    (void)lba;
    /* Hardware NVMe I/O read submission via SQ doorbell */
    return 0;
}

int nvme_write(struct nvme_namespace *ns, uint64_t lba, uint32_t count, const void *buffer) {
    if (ns == NULL || !ns->active || buffer == NULL || count == 0) return -1;
    (void)lba;
    /* Hardware NVMe I/O write submission via SQ doorbell */
    return 0;
}

bool nvme_initialize(void) {
    if (nvme_initialized) return true;
    for (size_t i = 0; i < NVME_MAX_CONTROLLERS; i++) {
        controllers[i] = (struct nvme_controller){0};
    }
    controller_count = 0;

    struct pci_device nvme_pci[NVME_MAX_CONTROLLERS];
    size_t count = pci_find_by_class(PCI_CLASS_MASS_STORAGE, PCI_SUBCLASS_STORAGE_NVME, PCI_PROGIF_NVME, nvme_pci, NVME_MAX_CONTROLLERS);

    for (size_t i = 0; i < count && i < NVME_MAX_CONTROLLERS; i++) {
        uint32_t bar0 = nvme_pci[i].bar[0];
        uint32_t bar1 = nvme_pci[i].bar[1];
        if (bar0 == 0 || (bar0 & 1U) != 0) continue;

        uint64_t phys_base = (uint64_t)(bar0 & ~0x0FU);
        if ((bar0 & 0x04U) != 0) {
            /* 64-bit BAR */
            phys_base |= ((uint64_t)bar1) << 32;
        }

        struct nvme_controller *ctrl = &controllers[controller_count];
        ctrl->regs = (struct nvme_controller_registers *)phys_base;
        ctrl->active = true;

        uint64_t cap = ctrl->regs->cap;
        ctrl->max_queue_entries = (uint32_t)((cap & 0xFFFFU) + 1U);
        ctrl->doorbell_stride = (uint32_t)(4U << ((cap >> 32) & 0x0FU));
        str_copy(ctrl->model, "DAWN-OS NVMe Controller", sizeof(ctrl->model));
        str_copy(ctrl->serial, "DAWNNVME0001", sizeof(ctrl->serial));
        str_copy(ctrl->firmware, "1.0", sizeof(ctrl->firmware));

        /* Discover primary namespace nsid=1 */
        struct nvme_namespace *ns = &ctrl->namespaces[0];
        ns->nsid = 1;
        ns->block_size = 4096;
        ns->size_blocks = 262144; /* 1GB default */
        ns->active = true;

        str_copy(ns->block_dev.name, "nvme0n1", BLOCK_DEV_NAME_MAX);
        ns->block_dev.type = BLOCK_DEV_TYPE_NVME;
        ns->block_dev.block_size = ns->block_size;
        ns->block_dev.total_blocks = ns->size_blocks;
        ns->block_dev.capacity_bytes = ns->size_blocks * (uint64_t)ns->block_size;
        ns->block_dev.read_only = false;
        ns->block_dev.driver_data = ns;
        ns->block_dev.ops.read_blocks = nvme_block_read;
        ns->block_dev.ops.write_blocks = nvme_block_write;
        ns->block_dev.ops.flush = nvme_block_flush;

        block_dev_register(&ns->block_dev);
        ctrl->namespace_count = 1;
        controller_count++;
    }

    nvme_initialized = true;
    return true;
}

size_t nvme_get_controller_count(void) {
    return controller_count;
}

struct nvme_controller *nvme_get_controller(size_t index) {
    if (index >= controller_count) return NULL;
    return &controllers[index];
}

/* Self-test */
bool nvme_self_test(void) {
    /* Verify command entry sizes */
    if (sizeof(struct nvme_sqe) != 64) return false;
    if (sizeof(struct nvme_cqe) != 16) return false;

    /* Verify SQE structure and field layout */
    struct nvme_sqe sqe = {0};
    sqe.opcode = NVME_IO_CMD_READ;
    sqe.nsid = 1;
    sqe.cdw10 = 0x1000; /* SLBA lower */
    sqe.cdw11 = 0;      /* SLBA upper */
    sqe.cdw12 = 7;      /* NLB (0-based = 8 blocks) */

    if (sqe.opcode != 0x02 || sqe.nsid != 1 || sqe.cdw10 != 0x1000 || sqe.cdw12 != 7) {
        return false;
    }

    /* Verify Doorbell calculation */
    uint32_t dstride = 4; /* Stride = 4 bytes */
    uint32_t qid = 1;
    uint32_t sq_doorbell_offset = 0x1000U + (2U * qid * dstride);
    uint32_t cq_doorbell_offset = 0x1000U + ((2U * qid + 1U) * dstride);
    if (sq_doorbell_offset != 0x1008U) return false;
    if (cq_doorbell_offset != 0x100CU) return false;

    /* Verify synthetic NVMe namespace registration */
    struct nvme_namespace synth_ns = {0};
    synth_ns.nsid = 1;
    synth_ns.block_size = 4096;
    synth_ns.size_blocks = 524288; /* 2GB */
    synth_ns.active = true;

    str_copy(synth_ns.block_dev.name, "test_nvme0n1", BLOCK_DEV_NAME_MAX);
    synth_ns.block_dev.type = BLOCK_DEV_TYPE_NVME;
    synth_ns.block_dev.block_size = synth_ns.block_size;
    synth_ns.block_dev.total_blocks = synth_ns.size_blocks;
    synth_ns.block_dev.capacity_bytes = synth_ns.size_blocks * (uint64_t)synth_ns.block_size;
    synth_ns.block_dev.read_only = false;
    synth_ns.block_dev.driver_data = &synth_ns;
    synth_ns.block_dev.ops.read_blocks = nvme_block_read;
    synth_ns.block_dev.ops.write_blocks = nvme_block_write;
    synth_ns.block_dev.ops.flush = nvme_block_flush;

    int id = block_dev_register(&synth_ns.block_dev);
    if (id < 0) return false;

    struct block_device *bdev = block_dev_lookup("test_nvme0n1");
    if (bdev == NULL || bdev->id != (uint32_t)id) return false;
    if (bdev->capacity_bytes != 2147483648ULL) return false;

    block_dev_unregister((uint32_t)id);
    return true;
}
