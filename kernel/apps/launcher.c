/*
 * Vib-OS Application Launcher
 * 
 * Provides kernel API and launches embedded applications
 */

#include "apps/kapi.h"
#include "printk.h"
#include "mm/kmalloc.h"

/* Display structure from window.c */
struct display {
    uint32_t *framebuffer;
    uint32_t *backbuffer;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
};

/* External references */
extern struct display *gui_get_display(void);
extern void mouse_get_position(int *x, int *y);
extern int mouse_get_buttons(void);
extern int uart_getc_nonblock(void);
extern void uart_putc(char c);

/* Timer ticks counter */
static volatile uint64_t uptime_ticks = 0;

/* Global kernel API instance */
static kapi_t global_kapi;

/* ===================================================================== */
/* KAPI Implementation Functions */
/* ===================================================================== */

static void kapi_putc(char c) {
    uart_putc(c);
}

static void kapi_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

static int kapi_getc(void) {
    return uart_getc_nonblock();
}

static int kapi_has_key(void) {
    return uart_getc_nonblock() >= 0 ? 1 : 0;
}

static void kapi_clear(void) {
    /* Clear framebuffer to black */
    struct display *d = gui_get_display();
    if (d && d->framebuffer) {
        for (uint32_t i = 0; i < d->width * d->height; i++) {
            d->framebuffer[i] = 0;
        }
    }
}

static int kapi_get_key(void) {
    return uart_getc_nonblock();
}

static void kapi_mouse_get_pos(int *x, int *y) {
    mouse_get_position(x, y);
}

static uint8_t kapi_mouse_get_buttons(void) {
    return (uint8_t)mouse_get_buttons();
}

static int last_mouse_x = 0, last_mouse_y = 0;
static void kapi_mouse_get_delta(int *dx, int *dy) {
    int x, y;
    mouse_get_position(&x, &y);
    *dx = x - last_mouse_x;
    *dy = y - last_mouse_y;
    last_mouse_x = x;
    last_mouse_y = y;
}

static uint64_t kapi_get_uptime_ticks(void) {
    return uptime_ticks;
}

static void kapi_sleep_ms(uint32_t ms) {
    /* Simple busy-wait sleep */
    for (volatile uint32_t i = 0; i < ms * 10000; i++) { }
}

static void *kapi_malloc(size_t size) {
    return kmalloc(size);
}

static void kapi_free(void *ptr) {
    kfree(ptr);
}

/* File I/O stubs - TODO: implement with VFS */
static void *kapi_open(const char *path) {
    (void)path;
    return NULL; /* Not implemented yet */
}

static void kapi_close(void *handle) {
    (void)handle;
}

static int kapi_read(void *handle, void *buf, size_t count, size_t offset) {
    (void)handle; (void)buf; (void)count; (void)offset;
    return -1;
}

static int kapi_write(void *handle, const void *buf, size_t count) {
    (void)handle; (void)buf; (void)count;
    return -1;
}

static int kapi_file_size(void *handle) {
    (void)handle;
    return 0;
}

static int kapi_create(const char *path) {
    (void)path;
    return -1;
}

static int kapi_delete(const char *path) {
    (void)path;
    return -1;
}

static int kapi_rename(const char *old, const char *new) {
    (void)old; (void)new;
    return -1;
}

static void kapi_exit(int status) {
    printk(KERN_INFO "[APP] Exit with status %d\n", status);
    /* Return to kernel - in real userspace, this would terminate the process */
}

/* Run an app and wait for completion */
static int kapi_exec(const char *path) {
    printk(KERN_INFO "[KAPI] exec: %s\n", path);
    return app_run(path, 0, 0);
}

/* Run an app in background */
static int kapi_spawn(const char *path) {
    printk(KERN_INFO "[KAPI] spawn: %s\n", path);
    /* For now, same as exec - no true multitasking yet */
    return app_run(path, 0, 0);
}

/* Yield CPU to other tasks */
static void kapi_yield(void) {
    /* Placeholder - would call scheduler */
    for (volatile int i = 0; i < 1000; i++) { }
}

static void kapi_uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

/* ===================================================================== */
/* Initialize Kernel API */
/* ===================================================================== */

void kapi_init(kapi_t *api) {
    struct display *d = gui_get_display();
    api->version = 1;
    
    /* Framebuffer */
    api->fb_base = d ? d->framebuffer : NULL;
    api->fb_width = d ? d->width : 0;
    api->fb_height = d ? d->height : 0;
    api->fb_pitch = d ? d->pitch : 0;
    
    /* Memory */
    api->malloc = kapi_malloc;
    api->free = kapi_free;
    
    /* Console */
    api->putc = kapi_putc;
    api->puts = kapi_puts;
    api->getc = kapi_getc;
    api->has_key = kapi_has_key;
    api->clear = kapi_clear;
    
    /* Keyboard */
    api->get_key = kapi_get_key;
    
    /* Mouse */
    api->mouse_get_pos = kapi_mouse_get_pos;
    api->mouse_get_buttons = kapi_mouse_get_buttons;
    api->mouse_get_delta = kapi_mouse_get_delta;
    
    /* Timing */
    api->get_uptime_ticks = kapi_get_uptime_ticks;
    api->sleep_ms = kapi_sleep_ms;
    
    /* File I/O */
    api->open = kapi_open;
    api->close = kapi_close;
    api->read = kapi_read;
    api->write = kapi_write;
    api->file_size = kapi_file_size;
    api->create = kapi_create;
    api->delete = kapi_delete;
    api->rename = kapi_rename;
    
    /* Process */
    api->exit = kapi_exit;
    api->exec = kapi_exec;
    api->spawn = kapi_spawn;
    api->yield = kapi_yield;
    
    /* Debug */
    api->uart_puts = kapi_uart_puts;
    
    printk(KERN_INFO "[KAPI] Kernel API initialized (fb=%dx%d)\n", api->fb_width, api->fb_height);
}

/* ===================================================================== */
/* Application Registry - Embedded Apps */
/* ===================================================================== */

/* Tick counter for timing */
void kapi_tick(void) {
    uptime_ticks++;
}

/* Get the global kapi */
kapi_t *kapi_get(void) {
    static int initialized = 0;
    if (!initialized) {
        kapi_init(&global_kapi);
        initialized = 1;
    }
    return &global_kapi;
}

/* Simple test app */
static int test_app_main(kapi_t *api, int argc, char **argv) {
    (void)argc; (void)argv;
    
    api->puts("Hello from test app!\n");
    api->puts("Framebuffer: ");
    
    /* Draw a red rectangle on screen */
    if (api->fb_base) {
        for (int y = 100; y < 200; y++) {
            for (int x = 100; x < 300; x++) {
                api->fb_base[y * api->fb_width + x] = 0xFF0000;  /* Red */
            }
        }
        api->puts("Drew red rectangle!\n");
    }
    
    return 0;
}

/* App registry */
typedef struct {
    const char *name;
    app_main_fn main_fn;
} app_entry_t;

static app_entry_t app_registry[] = {
    { "test", test_app_main },
    { NULL, NULL }
};

/* Run an embedded application by name */
int app_run(const char *name, int argc, char **argv) {
    printk(KERN_INFO "[APP] Running: %s\n", name);
    
    /* Find app in registry */
    for (int i = 0; app_registry[i].name != NULL; i++) {
        /* Simple strcmp */
        const char *a = name;
        const char *b = app_registry[i].name;
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a == *b) {
            /* Match found */
            kapi_t *api = kapi_get();
            return app_registry[i].main_fn(api, argc, argv);
        }
    }
    
    printk(KERN_WARNING "[APP] App not found: %s\n", name);
    return -1;
}
