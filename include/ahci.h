/* SPDX-License-Identifier: Unlicense */
#ifndef DAWN_AHCI_H
#define DAWN_AHCI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "block_dev.h"
#include "pci.h"

#define AHCI_MAX_PORTS 32U

/* SATA Signatures */
#define SATA_SIG_ATA    0x00000101U
#define SATA_SIG_ATAPI  0xEB140101U
#define SATA_SIG_SEMB   0xC33C0101U
#define SATA_SIG_PM     0x96690101U

/* ATA Commands */
#define ATA_CMD_READ_DMA_EXT   0x25U
#define ATA_CMD_WRITE_DMA_EXT  0x35U
#define ATA_CMD_IDENTIFY       0xECU
#define ATA_CMD_IDENTIFY_PACKET 0xA1U

/* FIS Types */
#define FIS_TYPE_REG_H2D 0x27U

struct ahci_port_registers {
    volatile uint32_t clb;
    volatile uint32_t clbu;
    volatile uint32_t fb;
    volatile uint32_t fbu;
    volatile uint32_t is;
    volatile uint32_t ie;
    volatile uint32_t cmd;
    volatile uint32_t reserved0;
    volatile uint32_t tfd;
    volatile uint32_t sig;
    volatile uint32_t ssts;
    volatile uint32_t sctl;
    volatile uint32_t serr;
    volatile uint32_t sact;
    volatile uint32_t ci;
    volatile uint32_t sntf;
    volatile uint32_t fbs;
    volatile uint32_t reserved1[11];
    volatile uint32_t vendor[4];
} __attribute__((packed));

struct ahci_hba_memory {
    volatile uint32_t cap;
    volatile uint32_t ghc;
    volatile uint32_t is;
    volatile uint32_t pi;
    volatile uint32_t vs;
    volatile uint32_t ccc_ctl;
    volatile uint32_t ccc_pts;
    volatile uint32_t em_loc;
    volatile uint32_t em_ctl;
    volatile uint32_t cap2;
    volatile uint32_t bohc;
    volatile uint8_t  reserved[0xA0 - 0x2C];
    volatile uint8_t  vendor[0x100 - 0xA0];
    struct ahci_port_registers ports[AHCI_MAX_PORTS];
} __attribute__((packed));

struct ahci_cmd_header {
    uint8_t flags0;     /* CFL:5, A:1, W:1, P:1 */
    uint8_t flags1;     /* R:1, B:1, C:1, Rsvd:1, PMP:4 */
    uint16_t prdtl;     /* PRD table length in entries */
    volatile uint32_t prdbc; /* PRD byte count transferred */
    uint32_t ctba;      /* Command table descriptor base address */
    uint32_t ctbau;     /* Command table base address upper 32-bits */
    uint32_t reserved[4];
} __attribute__((packed));

struct ahci_prdt_entry {
    uint32_t dba;       /* Data base address */
    uint32_t dbau;      /* Data base address upper 32-bits */
    uint32_t reserved;
    uint32_t dbc;       /* Byte count (22-bit, 0-indexed: 4M max), I:1 */
} __attribute__((packed));

struct ahci_cmd_table {
    uint8_t cfis[64];
    uint8_t acmd[16];
    uint8_t reserved[48];
    struct ahci_prdt_entry prdt[1];
} __attribute__((packed));

struct fis_reg_h2d {
    uint8_t fis_type;   /* FIS_TYPE_REG_H2D */
    uint8_t pmport_c;   /* Port multiplier:4, Reserved:3, C:1 (1=Command) */
    uint8_t command;    /* ATA Command */
    uint8_t feature_low;
    uint8_t lba0;
    uint8_t lba1;
    uint8_t lba2;
    uint8_t device;     /* 1 << 6 for LBA mode */
    uint8_t lba3;
    uint8_t lba4;
    uint8_t lba5;
    uint8_t feature_high;
    uint8_t count_low;
    uint8_t count_high;
    uint8_t icc;
    uint8_t control;
    uint8_t reserved[4];
} __attribute__((packed));

struct ahci_device {
    uint32_t port_number;
    uint32_t signature;
    uint64_t sector_count;
    char model[41];
    char serial[21];
    struct block_device block_dev;
    bool active;
};

bool ahci_initialize(void);
size_t ahci_get_port_count(void);
struct ahci_device *ahci_get_device(uint32_t index);
int ahci_read_sectors(struct ahci_device *dev, uint64_t lba, uint32_t count, void *buf);
int ahci_write_sectors(struct ahci_device *dev, uint64_t lba, uint32_t count, const void *buf);

bool ahci_self_test(void);

#endif /* DAWN_AHCI_H */
