/*
 * Vib-OS - PCI Driver
 * 
 * Basic PCI Express ECAM support for QEMU 'virt' machine
 */

#ifndef DRIVERS_PCI_H
#define DRIVERS_PCI_H

#include "types.h"

/* QEMU ARM64 'virt' machine PCI High ECAM Base */
/* Note: Depending on memory map, this might be 0x4010000000 or 0x3f000000 (Low ECAM) */
/* For >3GB RAM, High ECAM is usually present. We'll check High first. */
#define PCI_ECAM_BASE   0x4010000000ULL

/* Common PCI Register Offsets */
#define PCI_VENDOR_ID   0x00
#define PCI_DEVICE_ID   0x02
#define PCI_COMMAND     0x04
#define PCI_STATUS      0x06
#define PCI_CLASS_REV   0x08
#define PCI_HEADER_TYPE 0x0E
#define PCI_BAR0        0x10
#define PCI_BAR1        0x14
#define PCI_INTERRUPT   0x3C

/* Command bits */
#define PCI_CMD_IO      0x01
#define PCI_CMD_MEM     0x02
#define PCI_CMD_BUS_MASTER 0x04

/* Device struct placeholder */
typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint64_t bar0;
    uint32_t irq;
} pci_device_t;

void pci_init(void);
uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);

#endif
