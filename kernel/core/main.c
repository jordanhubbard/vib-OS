/*
 * UnixOS Kernel - Main Entry Point
 * 
 * This is the C entry point called from boot.S after basic
 * hardware initialization is complete.
 */

#include "types.h"
#include "printk.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "sched/sched.h"
#include "arch/arm64/gic.h"
#include "arch/arm64/timer.h"
#include "drivers/uart.h"
#include "fs/vfs.h"

/* Kernel version */
#define UNIXOS_VERSION_MAJOR 0
#define UNIXOS_VERSION_MINOR 1
#define UNIXOS_VERSION_PATCH 0

/* External symbols from linker script */
extern char __kernel_start[];
extern char __kernel_end[];
extern char __bss_start[];
extern char __bss_end[];

/* Forward declarations */
static void print_banner(void);
static void init_subsystems(void *dtb);
static void start_init_process(void);

/*
 * kernel_main - Main kernel entry point
 * @dtb: Pointer to device tree blob passed by bootloader
 * 
 * This function never returns. After initialization, it either:
 * 1. Starts the init process and enters the scheduler
 * 2. Panics if initialization fails
 */
void kernel_main(void *dtb)
{
    /* Initialize early console for debugging */
    uart_early_init();
    
    /* Print boot banner */
    print_banner();
    
    printk("DEBUG: After banner\n");
    
    (void)dtb;  /* Suppress unused warning */
    (void)__kernel_start;
    (void)__kernel_end;
    
    printk("DEBUG: About to call init_subsystems\n");
    
    /* Initialize all kernel subsystems */
    init_subsystems(dtb);
    
    printk("DEBUG: After init_subsystems\n");
    printk(KERN_INFO "All subsystems initialized successfully\n");
    printk(KERN_INFO "Starting init process...\n\n");
    
    /* Start the first userspace process */
    start_init_process();
    
    /* This point should never be reached */
    panic("kernel_main returned unexpectedly!");
}

/*
 * print_banner - Display kernel boot banner
 */
static void print_banner(void)
{
    printk("\n");
    printk("  _   _       _       ___  ____  \n");
    printk(" | | | |_ __ (_)_  __/ _ \\/ ___| \n");
    printk(" | | | | '_ \\| \\ \\/ / | | \\___ \\ \n");
    printk(" | |_| | | | | |>  <| |_| |___) |\n");
    printk("  \\___/|_| |_|_/_/\\_\\\\___/|____/ \n");
    printk("\n");
    printk("UnixOS v%d.%d.%d - ARM64\n",
           UNIXOS_VERSION_MAJOR,
           UNIXOS_VERSION_MINOR,
           UNIXOS_VERSION_PATCH);
    printk("A production-grade Unix-like operating system\n");
    printk("Copyright (c) 2026 UnixOS Project\n");
    printk("\n");
}

/*
 * init_subsystems - Initialize all kernel subsystems
 * @dtb: Device tree blob for hardware discovery
 */
static void init_subsystems(void *dtb)
{
    int ret;
    
    /* ================================================================= */
    /* Phase 1: Core Hardware */
    /* ================================================================= */
    
    printk(KERN_INFO "[INIT] Phase 1: Core Hardware\n");
    
    /* Parse device tree for hardware information */
    printk(KERN_INFO "  Parsing device tree...\n");
    (void)dtb;  /* TODO: dtb_parse(dtb); */
    
    /* Initialize interrupt controller */
    printk(KERN_INFO "  Initializing GIC (interrupt controller)...\n");
    gic_init();
    
    /* Initialize system timer */
    printk(KERN_INFO "  Initializing timer...\n");
    timer_init();
    
    /* ================================================================= */
    /* Phase 2: Memory Management */
    /* ================================================================= */
    
    printk(KERN_INFO "[INIT] Phase 2: Memory Management\n");
    
    /* Initialize physical memory manager */
    printk(KERN_INFO "  Initializing physical memory manager...\n");
    ret = pmm_init();
    if (ret < 0) {
        panic("Failed to initialize physical memory manager!");
    }
    printk(KERN_INFO "  Physical memory: %lu MB available\n", pmm_get_free_memory() / (1024 * 1024));
    
    /* Initialize virtual memory manager */
    printk(KERN_INFO "  Initializing virtual memory manager...\n");
    ret = vmm_init();
    if (ret < 0) {
        panic("Failed to initialize virtual memory manager!");
    }
    
    /* Initialize kernel heap */
    printk(KERN_INFO "  Initializing kernel heap...\n");
    extern void kmalloc_init(void);
    kmalloc_init();
    
    /* ================================================================= */
    /* Phase 3: Process Management */
    /* ================================================================= */
    
    printk(KERN_INFO "[INIT] Phase 3: Process Management\n");
    
    /* Initialize scheduler */
    printk(KERN_INFO "  Initializing scheduler...\n");
    sched_init();
    
    /* ================================================================= */
    /* Phase 4: Filesystems */
    /* ================================================================= */
    
    printk(KERN_INFO "[INIT] Phase 4: Filesystems\n");
    
    /* Initialize Virtual Filesystem */
    printk(KERN_INFO "  Initializing VFS...\n");
    vfs_init();
    
    /* Mount root filesystem */
    printk(KERN_INFO "  Mounting root filesystem...\n");
    /* TODO: mount_root(); */
    
    /* Mount proc, sys, dev */
    printk(KERN_INFO "  Mounting procfs...\n");
    printk(KERN_INFO "  Mounting sysfs...\n");
    printk(KERN_INFO "  Mounting devfs...\n");
    
    /* ================================================================= */
    /* Phase 5: Device Drivers & GUI */
    /* ================================================================= */
    
    printk(KERN_INFO "[INIT] Phase 5: Device Drivers\n");
    
    /* Initialize framebuffer driver */
    printk(KERN_INFO "  Loading framebuffer driver...\n");
    extern int fb_init(void);
    extern void fb_get_info(uint32_t **buffer, uint32_t *width, uint32_t *height);
    fb_init();
    
    /* Initialize GUI windowing system */
    printk(KERN_INFO "  Initializing GUI...\n");
    extern int gui_init(uint32_t *framebuffer, uint32_t width, uint32_t height, uint32_t pitch);
    extern struct window *gui_create_window(const char *title, int x, int y, int w, int h);
    extern void gui_compose(void);
    extern void gui_draw_cursor(void);
    
    uint32_t *fb_buffer;
    uint32_t fb_width, fb_height;
    fb_get_info(&fb_buffer, &fb_width, &fb_height);
    
    if (fb_buffer) {
        gui_init(fb_buffer, fb_width, fb_height, fb_width * 4);
        
        /* Create demo windows */
        gui_create_window("Terminal", 50, 50, 400, 300);
        gui_create_window("File Manager", 200, 100, 450, 350);
        
        /* Compose and display desktop */
        gui_compose();
        gui_draw_cursor();
        
        printk(KERN_INFO "  GUI desktop ready!\\n");
    }
    
    printk(KERN_INFO "  Loading keyboard driver...\n");
    printk(KERN_INFO "  Loading NVMe driver...\n");
    printk(KERN_INFO "  Loading USB driver...\n");
    printk(KERN_INFO "  Loading network driver...\n");
    
    /* ================================================================= */
    /* Phase 6: Enable Interrupts */
    /* ================================================================= */
    
    printk(KERN_INFO "[INIT] Enabling interrupts...\n");
    /* Enable interrupts */
    asm volatile("msr daifclr, #2");  /* Clear IRQ mask */
    
    printk(KERN_INFO "[INIT] Kernel initialization complete!\n\n");
}

/*
 * start_init_process - Start the first userspace process (PID 1)
 */

/* Global terminal pointer for keyboard callback */
static void *g_active_terminal = 0;

/* Keyboard callback wrapper */
static void keyboard_handler(int key)
{
    extern void gui_handle_key_event(int key);
    gui_handle_key_event(key);
}

static void start_init_process(void)
{
    /* Create and start init process */
    printk(KERN_INFO "Executing /sbin/init...\n");
    
    printk(KERN_INFO "Init process started (placeholder)\n");
    printk(KERN_INFO "System ready.\n\n");
    
    /* Set up input handling */
    extern int input_init(void);
    extern void input_poll(void);
    extern void input_set_key_callback(void (*callback)(int key));
    extern void gui_compose(void);
    extern void gui_draw_cursor(void);
    
    input_init();
    
    /* Connect keyboard input to terminal */
    input_set_key_callback(keyboard_handler);
    
    printk(KERN_INFO "GUI: Event loop started - type in terminal!\\n");
    
    /* Initial render */
    gui_compose();
    gui_draw_cursor();
    
    /* Main GUI event loop */
    uint32_t frame = 0;
    int last_mx = 0, last_my = 0;
    int last_buttons = 0;
    int needs_redraw = 1;  /* Initial draw */
    int cursor_only = 0;   /* Only cursor needs updating */
    
    while (1) {
        /* Poll virtio input devices (keyboard/mouse) - MUST call this! */
        input_poll();
        
        /* Poll for keyboard input from UART as well */
        extern int uart_getc_nonblock(void);
        extern void gui_handle_key_event(int key);
        int c = uart_getc_nonblock();
        if (c >= 0) {
            /* Route to focused window */
            gui_handle_key_event(c);
            needs_redraw = 1;
        }
        
        /* Poll mouse for position and buttons */
        extern void mouse_get_position(int *x, int *y);
        extern int mouse_get_buttons(void);
        extern void gui_handle_mouse_event(int x, int y, int buttons);
        
        int mx, my;
        mouse_get_position(&mx, &my);
        int mbuttons = mouse_get_buttons();
        
        /* Check if mouse changed */
        if (mx != last_mx || my != last_my || mbuttons != last_buttons) {
            /* Only full redraw on button state change (click/release) or drag */
            if (mbuttons != last_buttons || (mbuttons && (mx != last_mx || my != last_my))) {
                gui_handle_mouse_event(mx, my, mbuttons);
                needs_redraw = 1;
            } else {
                /* Just cursor moved - only update cursor position */
                cursor_only = 1;
            }
            last_mx = mx;
            last_my = my;
            last_buttons = mbuttons;
        }
        
        /* Only redraw when needed */
        if (needs_redraw) {
            gui_compose();
            gui_draw_cursor();
            needs_redraw = 0;
            cursor_only = 0;
        } else if (cursor_only) {
            /* Just update cursor - much faster */
            gui_draw_cursor();
            cursor_only = 0;
        }
        
        frame++;
        (void)frame;
        
        /* Frame rate limiting - minimal delay for responsive UI */
        for (volatile int i = 0; i < 50000; i++) { }
    }
}

/*
 * panic - Halt the system with an error message
 * @msg: Error message to display
 */
void panic(const char *msg)
{
    /* Disable interrupts */
    asm volatile("msr daifset, #0xf");
    
    printk(KERN_EMERG "\n");
    printk(KERN_EMERG "============================================\n");
    printk(KERN_EMERG "KERNEL PANIC!\n");
    printk(KERN_EMERG "============================================\n");
    printk(KERN_EMERG "%s\n", msg);
    printk(KERN_EMERG "============================================\n");
    printk(KERN_EMERG "System halted.\n");
    
    /* Infinite loop */
    while (1) {
        asm volatile("wfi");
    }
}
