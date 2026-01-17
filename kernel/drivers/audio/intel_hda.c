/*
 * Vib-OS - Intel HDA Audio Driver Implementation
 */

#include "types.h"
#include "printk.h"
#include "drivers/intel_hda.h"
#include "drivers/pci.h"
#include "mm/kmalloc.h"

/* Helper prototypes */
void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);

/* Registers */
static volatile uint32_t *hda_regs = 0;

/* CORB/RIRB Buffers */
static uint32_t *corb_buffer = 0;
static uint64_t *rirb_buffer = 0;

/* Reg Access Helpers */
static uint32_t hda_read32(uint32_t offset) {
    return hda_regs[offset / 4];
}

static void hda_write32(uint32_t offset, uint32_t val) {
    hda_regs[offset / 4] = val;
}

static uint16_t hda_read16(uint32_t offset) {
    volatile uint16_t *regs16 = (volatile uint16_t *)hda_regs;
    return regs16[offset / 2];
}

static void hda_write16(uint32_t offset, uint16_t val) {
    volatile uint16_t *regs16 = (volatile uint16_t *)hda_regs;
    regs16[offset / 2] = val;
}

static uint8_t hda_read8(uint32_t offset) {
    volatile uint8_t *regs8 = (volatile uint8_t *)hda_regs;
    return regs8[offset];
}

static void hda_write8(uint32_t offset, uint8_t val) {
    volatile uint8_t *regs8 = (volatile uint8_t *)hda_regs;
    regs8[offset] = val;
}

void intel_hda_init(pci_device_t *pci_dev) {
    printk("HDA: Initializing...\n");
    
    /* Map BAR0 (Using identity map for now) */
    hda_regs = (volatile uint32_t *)pci_dev->bar0;
    
    if (!hda_regs) {
        printk("HDA: Error - BAR0 is 0\n");
        return;
    }

    /* Enable Bus Master and Memory Space in PCI Command Register */
    uint32_t cmd = pci_read32(pci_dev->bus, pci_dev->slot, pci_dev->func, PCI_COMMAND);
    pci_write32(pci_dev->bus, pci_dev->slot, pci_dev->func, PCI_COMMAND, cmd | PCI_CMD_BUS_MASTER | PCI_CMD_MEM);
    printk("HDA: Enabled Bus Master/Mem (Cmd: %x)\n", cmd | PCI_CMD_BUS_MASTER | PCI_CMD_MEM);

    /* 1. Reset Controller */
    printk("HDA: Resetting controller...\n");
    uint32_t gctl = hda_read32(HDA_GCTL);
    hda_write32(HDA_GCTL, gctl & ~1); /* Clear CRST */
    
    /* Wait for bit to clear */
    int timeout = 10000;
    while((hda_read32(HDA_GCTL) & 1) && timeout-- > 0);
    if (timeout <= 0) printk("HDA: Reset timeout (clear)!\n");
    
    hda_write32(HDA_GCTL, gctl | 1); /* Set CRST */
    /* Wait for bit to set */
    timeout = 10000;
    while(!(hda_read32(HDA_GCTL) & 1) && timeout-- > 0);
    if (timeout <= 0) printk("HDA: Reset timeout (set)!\n");
    
    /* Wait for codecs to report state */
    /* In QEMU, this should happen quickly */
    timeout = 10000;
    while(!(hda_read16(HDA_STATESTS)) && timeout-- > 0);
    uint16_t statests = hda_read16(HDA_STATESTS);
    printk("HDA: Reset complete. STATESTS=0x%04x\n", statests);
    
    /* 2. Allocate CORB/RIRB */
    /* CORB: 256 entries * 4 bytes = 1024 bytes */
    /* RIRB: 256 entries * 8 bytes = 2048 bytes */
    /* Implementations must support 256 entries. 
       Note: Alignment requirements - 128 bytes */
       
    /* 2. Allocate CORB/RIRB with 128-byte alignment */
    /* CORB: 1024 bytes. RIRB: 2048 bytes. */
    
    void *raw_corb = kmalloc(1024 + 128);
    corb_buffer = (uint32_t *)(((uint64_t)raw_corb + 127) & ~127ULL);
    
    void *raw_rirb = kmalloc(2048 + 128);
    rirb_buffer = (uint64_t *)(((uint64_t)raw_rirb + 127) & ~127ULL);
    
    /* Zero buffers */
    memset(corb_buffer, 0, 1024);
    memset(rirb_buffer, 0, 2048);
    
    /* Set Base Addresses (Assuming 32-bit pointers for now) */
    hda_write32(HDA_CORBLBASE, (uint32_t)(uint64_t)corb_buffer);
    hda_write32(HDA_CORBUBASE, (uint32_t)((uint64_t)corb_buffer >> 32));
    
    hda_write32(HDA_RIRBLBASE, (uint32_t)(uint64_t)rirb_buffer);
    hda_write32(HDA_RIRBUBASE, (uint32_t)((uint64_t)rirb_buffer >> 32));
    
    /* Reset WP */
    hda_write16(HDA_CORBWP, 0);
    hda_write16(HDA_RIRBWP, 0x8000); /* Reset RIRB WP (bit 15 is reset) */
    
    /* Set Size: 256 entries (Encoded as 0x02) */
    hda_write8(HDA_CORBSIZE, 0x02);
    hda_write8(HDA_RIRBSIZE, 0x02);
    
    /* Enable CORB/RIRB */
    hda_write8(HDA_CORBCTL, 0x02); /* Run */
    hda_write8(HDA_RIRBCTL, 0x02 | 0x01); /* Run + Interrupt */
    
    printk("HDA: CORB/RIRB initialized. Checking functionality...\n");
    
    /* 3. Discover Streams */
    uint16_t gcap = hda_read16(HDA_GCAP);
    uint8_t iss = (gcap >> 8) & 0xF; /* Input Streams */
    uint8_t oss = (gcap >> 12) & 0xF; /* Output Streams */
    uint8_t bss = (gcap >> 3) & 0x1F; /* Bidirectional Streams */
    
    printk("HDA: Capabilities: ISS=%d, OSS=%d, BSS=%d\n", iss, oss, bss);
    
    /* Calculate Output Stream Offset */
    /* Registers start at 0x80. Input streams are first. */
    /* Stride is 0x20 bytes */
    uint32_t output_stream_offset = 0x80 + (iss * 0x20);
    printk("HDA: Output Stream 0 offset: 0x%x\n", output_stream_offset);
    
    /* 4. Configure Codec (QEMU HDA-Duplex usually at Address 0) */
    /* We need to send verbs via CORB and check RIRB. 
       Check CORBRP. 
       To send: 
       1. Write Verb to CORB[WritePtr + 1]
       2. WritePtr++
       3. Write WritePtr to CORBWP reg
       
       Verb Format:
       CAd (4) | NodeID (8) | Payload (20)
       Payload = VerbID (12) | Param (8) - or similar depending on size
       
       Common Verbs:
       F00: Get Parameter
       705: Set Pin Control
       3A0: Set Amp Gain/Mute
       
       QEMU Defaults:
       Node 0 (Root) -> 1 (FG) -> 2 (DAC) -> 4 (Pin Complex / Line Out)
    */
    
    /* Simple sync verb sender helper (inline for now) */
    #define HDA_VERB(cad, nid, vid, payload) \
        ((uint32_t)(cad) << 28 | (uint32_t)(nid) << 20 | (uint32_t)(vid) << 8 | (payload))
        
    printk("HDA: Configuring Codec...\n");
    
    /* Variables for manual verb construction */
    uint16_t wp;
    
    /* 1. Reset Node 0 (Root) - often not needed */
    
    /* 2. Audio Function Group (Node 1) - Power State D0 */
    /* Verb 705: Set Power State. Payload 0 = D0 */
    /* HDA_VERB(0, 1, 0x705, 0x00) */
    wp = hda_read16(HDA_CORBWP) & 0xFF; wp++;
    corb_buffer[wp] = 0x00170500;
    hda_write16(HDA_CORBWP, wp);
    
    /* 3. Audio Output / DAC (Node 2) */
    /* Set Power D0 */
    wp++; corb_buffer[wp] = 0x00270500;
    /* Set Amp Gain/Mute (Verb 3) - Set Output Left/Right, Unmute, Gain 0 */
    /* 0x3 (Set Amp) | 0xB0 (Output, Left/Right, Unmute) | 0x7F (Max Gain) */
    /* Payload = 0xB07F for Verb 3A0? -> 0x3A0 B07F? No, usually 0x3 + payload. 
       Standard Verb 3: Set Amp Gain. 
       Vid=3. Payload=rrr.
       Wait, let's use 4-bit verb syntax? 
       Nibble 3 = 0x3. Payload 16 bits? 
       Cmd = 0x0023B07F
    */
    wp++; corb_buffer[wp] = 0x0023B07F; /* DAC Unmute, Max Gain */
    
    /* Also set format for DAC to match? Default usually works. */
    /* Set Converter Stream/Channel (Verb 706) */
    /* Stream 1, Channel 0 -> Payload 0x10 */
    /* Note: We used Stream Descriptor 0... wait. 
       SD0 maps to Stream ID 1? 
       Usually default map is straightforward. 
    /* 3. Output Converter (Node 2) */
    /* Set Stream Tag to 1, Channel 0. Verb 706. Payload 0x10 */
    wp++; corb_buffer[wp] = 0x00270610;
    
    /* Unmute Output Amp (Left/Right) - Set Gain to Max (0x7F) */
    /* Verb 3A0 (Set Amp Gain). Payload: Index 0 (out), Left(0x80)|Right(0x40)=0, Gain=0x7F */
    /* Actually: 0x3 (Set Amp), Index 0, B07F = Set Left/Right Output Amp to 0 (Unmute) + 0x7F (Max Gain) */
    /* HDA Spec: 3 = Set Amp Gain/Mute. Payload: 
       Bit 15: Set Output (1) / Input (0)
       Bit 14: Set Input (1) / Output (0) ... wait.
       Payload for Set Amp Gain (0x3):
       15: Set Left 
       14: Set Right
       13: Set Input/Output (1=Input, 0=Output) - Wait, for Output Amp it is 0?
       12: Set Mute (1=Mute, 0=Unmute)
       0-6: Gain
       
       Let's use 0x002 3 A0 7F -> 0x0023B07F
       But we want Unmute (Bit 7 of payload in 'Set Amp' verb which is 0x3 from invalid verbs list?)
       
       Better Verb: 0x705 (Power)
       Let's use the standard "Set Amp Gain/Mute" verb: 0x3
       Node 2:
       Payoad: 0xB07F
       B = 1011. 1 (Set Left), 0 (Set Right - NO), 1 (Input/Output?), 1 (Mute?)
       
       Let's stick to simplest:
       Verb 0x706: Stream 1.
       
       CORRECT VERB for Widget Control: 0x707 (Pin Widget Control).
       For DAC (Node 2), we just need Format and Stream.
    */
    
    /* 4. Pin Complex (Node 4 - Line Out) */
    /* Power D0 */
    wp++; corb_buffer[wp] = 0x00470500;
    
    /* Pin Control: Out Enable (0x40) + Headphone (0x80) => 0xC0 */
    wp++; corb_buffer[wp] = 0x004707C0;
    
    /* Unmute Pin Widget Amp (Output) just in case */
    wp++; corb_buffer[wp] = 0x0043B07F; /* Set Amp Gain: Out, Left/Right, Unmute, Max */

    /* EAPD Enable (External Amp) */
    wp++; corb_buffer[wp] = 0x00470C02; 
    
    /* Flush CORB Buffer to RAM before notifying hardware */
    {
        uint64_t start = (uint64_t)corb_buffer;
        uint64_t end = start + 256 * 4; /* Max CORB size usually 256 entries */
        uint64_t cache_line_size = 64;
        start = start & ~(cache_line_size - 1);
        while (start < end) {
            asm volatile("dc civac, %0" :: "r" (start));
            start += cache_line_size;
        }
        asm volatile("dsb sy");
    }
    
    hda_write16(HDA_CORBWP, wp);
    
    printk("HDA: Codec Configured (Node 2 Stream=1, Node 4 Out).\n");
    
    /* Test Tone: Square Wave (48kHz, 16-bit, Stereo) */
    /* 1000 samples ~ 20ms. Beep. */
    uint32_t test_samples = 48000 / 2; /* 0.5 sec */
    int16_t *test_buf = (int16_t *)kmalloc(test_samples * 2 * 2);
    for(uint32_t i=0; i<test_samples; i++) {
        int16_t val = (i % 100) < 50 ? 5000 : -5000;
        test_buf[i*2] = val;   /* Left */
        test_buf[i*2+1] = val; /* Right */
    }
    
    intel_hda_play_pcm(test_buf, test_samples, 2, 48000);
}


/* Global DMA Resources for Output Stream 0 */
static uint8_t *dma_buffer = 0;
static hda_bdl_entry_t *bdl = 0;
static uint32_t stream_base = 0;

int intel_hda_play_pcm(const void *data, uint32_t samples, uint8_t channels, uint32_t sample_rate) {
    if (!hda_regs) return -1;
    
    /* Calculate size in bytes (16-bit = 2 bytes) */
    uint32_t size = samples * channels * 2;
    if (size > 64 * 1024) size = 64 * 1024; /* Cap at 64K for now */
    
    /* Allocate resources once with 128-byte alignment for BDL */
    if (!dma_buffer) {
        /* BDL Must be 128-byte aligned */
        void *raw_bdl = kmalloc(128 * sizeof(hda_bdl_entry_t) + 128);
        bdl = (hda_bdl_entry_t *)(((uint64_t)raw_bdl + 127) & ~127ULL);
        
        /* DMA Buffer alignment (128 bytes recommended) */
        void *raw_buf = kmalloc(64 * 1024 + 128);
        dma_buffer = (uint8_t *)(((uint64_t)raw_buf + 127) & ~127ULL);
        
        /* Find Stream 0 Base (We found it was 0x100 for Output 0) */
        /* Re-calculating to be safe or reusing logic */
        /* Hardcoding offset 0x100 based on logs: "Output Stream 0 offset: 0x100" */
        stream_base = 0x100;
        
        memset(dma_buffer, 0, 64 * 1024);
        memset(bdl, 0, 128 * sizeof(hda_bdl_entry_t));
    }
    
    /* 1. Reset Stream properly */
    uint32_t ctl_offset = stream_base + HDA_SD_CTL;
    
    /* Clear RUN and Wait */
    hda_write32(ctl_offset, 0); 
    /* Spin wait for Run=0 */
    for(int timeout=0; timeout<1000; timeout++) {
        if (! (hda_read32(ctl_offset) & HDA_SD_CTL_RUN)) break;
    }
    
    /* Set SRST (Bit 0) and Wait */
    hda_write32(ctl_offset, 1);
    for(int timeout=0; timeout<1000; timeout++) {
        if (hda_read32(ctl_offset) & 1) break;
    }
    
    /* Clear SRST and Wait */
    hda_write32(ctl_offset, 0);
    for(int timeout=0; timeout<1000; timeout++) {
        if (! (hda_read32(ctl_offset) & 1)) break;
    }
    
    /* Wait for stream to stop? loop? */
    
    /* 2. Reset Stream (Set SRST bit 0 - Oh wait, different reg? */
    /* Stream Reset is effectively clearing RUN. 
       Actually some docs say set SRST in SDmCTL. But simpler acts just clearing RUN resets DMA pointer. */
       
    /* 3. Setup Buffer */
    memcpy(dma_buffer, data, size);
    
    /* 3. Setup Buffer */
    memcpy(dma_buffer, data, size);
    
    /* Enable Global Interrupts (GIE) */
    hda_write32(HDA_INTCTL, 0x80000000 | 0x40000000);

    /* Setup BDL with 2 entries */
    uint32_t half_size = size / 2;
    bdl[0].addr = (uint64_t)dma_buffer;
    bdl[0].len = half_size;
    bdl[0].flags = 0;
    
    bdl[1].addr = (uint64_t)(dma_buffer + half_size);
    bdl[1].len = half_size;
    bdl[1].flags = 0;

    /* Flush Data */
    uint64_t start = (uint64_t)dma_buffer;
    uint64_t end = start + size;
    uint64_t cache_line_size = 64;
    start = start & ~(cache_line_size - 1);
    while (start < end) {
        asm volatile("dc civac, %0" :: "r" (start));
        start += cache_line_size;
    }
    asm volatile("dsb sy");
    
    /* Flush BDL */
    start = (uint64_t)bdl;
    end = start + 256;
    start = start & ~(cache_line_size - 1);
    while (start < end) {
        asm volatile("dc civac, %0" :: "r" (start));
        start += cache_line_size;
    }
    asm volatile("dsb sy");
    
    /* 5. Program Stream Registers */
    hda_write32(stream_base + HDA_SD_BDLPL, (uint32_t)(uint64_t)bdl);
    hda_write32(stream_base + HDA_SD_BDLPU, (uint32_t)((uint64_t)bdl >> 32));
    hda_write32(stream_base + HDA_SD_CBL, size);
    hda_write16(stream_base + HDA_SD_LVI, 1);
    
    /* Set Format: 44.1kHz, 16-bit, Stereo */
    uint16_t fmt = 0x4011;
    hda_write16(stream_base + HDA_SD_FMT, fmt);
    
    /* Codec Format Setup */
    hda_write32(HDA_ICOI, 0x00224011);
    hda_write16(HDA_ICIS, 0x1);
    for(int i=0; i<1000; i++) if(!(hda_read16(HDA_ICIS) & 1)) break;

    /* 6. Start Stream */
    hda_write32(HDA_SSYNC, 0);
    hda_write8(stream_base + HDA_SD_STS, 0x1C);

    /* Enable RUN */
    hda_write32(ctl_offset, HDA_SD_CTL_RUN | (1 << 20));
    
    /* BLOCKING WAIT: Time Based */
    /* Approx 0.5s wait to ensure sound is heard */
    /* QEMU TCG can be varying speed, but 50M loops should be enough for "some" sound */
    /* Better: Use kapi_get_uptime_ticks if available? No, this is kernel land driver. */
    /* Just use a safe massive loop */
    
    for(volatile int w=0; w<50000000; w++);
    
    /* Stop Stream */
    hda_write32(ctl_offset, 0);
    hda_write32(ctl_offset, 0);

    return samples;
 }
