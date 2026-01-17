/*
 * Vib-OS - PCI Driver Implementation
 * 
 * Scans ECAM to find devices.
 */

#include "types.h"
#include "printk.h"
#include "drivers/pci.h"
#include "drivers/intel_hda.h"

/* Helper to calculate ECAM address */
/* Bus 8 bits, Device 5 bits, Function 3 bits, Offset 12 bits */
static volatile uint32_t *pci_ecam_addr(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint64_t addr = PCI_ECAM_BASE |
                    ((uint64_t)bus << 20) |
                    ((uint64_t)slot << 15) |
                    ((uint64_t)func << 12) |
                    (offset & 0xFFF);
    return (volatile uint32_t *)addr;
}

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    volatile uint32_t *addr = pci_ecam_addr(bus, slot, func, offset);
    return *addr;
}

void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    volatile uint32_t *addr = pci_ecam_addr(bus, slot, func, offset);
    *addr = value;
}

void pci_init(void) {
    printk("PCI: Initializing High ECAM scan at 0x%llx...\n", PCI_ECAM_BASE);
    
    /* Brute force scan of Bus 0 */
    /* QEMU virt usually puts devices on Bus 0 */
    for (int slot = 0; slot < 32; slot++) {
        /* Check vendor */
        uint32_t vendor_dev = pci_read32(0, slot, 0, PCI_VENDOR_ID);
        uint16_t vendor = vendor_dev & 0xFFFF;
        uint16_t device = (vendor_dev >> 16) & 0xFFFF;
        
        if (vendor != 0xFFFF && vendor != 0x0000) {
            printk("PCI: Found %04x:%04x at 00:%02x.0\n", vendor, device, slot);
            
            /* Check if it's Intel HDA */
            if (vendor == HDA_VENDOR_ID && device == HDA_DEVICE_ID) {
                printk("PCI: Found Inteal HDA Audio Controller!\n");
                
                /* Create device struct */
                pci_device_t pci_dev;
                pci_dev.bus = 0;
                pci_dev.slot = slot;
                pci_dev.func = 0;
                pci_dev.vendor_id = vendor;
                pci_dev.device_id = device;
                
                /* Read BAR0 */
                uint32_t bar0_raw = pci_read32(0, slot, 0, PCI_BAR0);
                uint32_t flags = bar0_raw & 0xF;
                
                /* If BAR0 is unassigned (0) or we want to force allocation */
                if ((bar0_raw & 0xFFFFFFF0) == 0) {
                     printk("PCI: Unassigned BAR0 for device %02x (Flags %x). Allocating...\n", slot, flags);
                     
                     static uint32_t next_mmio_base = 0x10000000;
                     
                     /* Size the BAR */
                     pci_write32(0, slot, 0, PCI_BAR0, 0xFFFFFFFF);
                     uint32_t size_val = pci_read32(0, slot, 0, PCI_BAR0);
                     uint32_t size_mask = size_val & 0xFFFFFFF0;
                     uint32_t size = (~size_mask) + 1;
                     
                     /* Restore flags or check type */
                     bool is_64bit = (bar0_raw & 0x4);
                     
                     if (size == 0 || size > 0x10000000) size = 0x4000;
                     
                     printk("PCI: Size needed: %x bytes (64-bit: %d)\n", size, is_64bit);
                     
                     /* Write address */
                     pci_write32(0, slot, 0, PCI_BAR0, next_mmio_base);
                     if (is_64bit) {
                         pci_write32(0, slot, 0, PCI_BAR1, 0x00000000);
                     }
                     
                     pci_dev.bar0 = next_mmio_base;
                     
                     next_mmio_base += size;
                     if (next_mmio_base % 0x1000) next_mmio_base = (next_mmio_base + 0x1000) & ~0xFFF;
                } else {
                    pci_dev.bar0 = bar0_raw & 0xFFFFFFF0;
                }
                
                /* Read Interrupt Line */
                uint32_t irq_line = pci_read32(0, slot, 0, PCI_INTERRUPT);
                pci_dev.irq = irq_line & 0xFF;
                
                printk("PCI: HDA BAR0=0x%llx, IRQ=%d\n", pci_dev.bar0, pci_dev.irq);
                
                /* Initialize HDA */
                intel_hda_init(&pci_dev);
            }
        }
    }
    printk("PCI: Scan complete.\n");
}
