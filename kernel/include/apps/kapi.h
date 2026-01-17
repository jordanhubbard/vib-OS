/*
 * Vib-OS Kernel API for Userspace Applications
 * Based on VibeOS kapi structure
 * 
 * This provides a clean interface between kernel and apps
 */

#ifndef KAPI_H
#define KAPI_H

#include "types.h"

/* Kernel API structure - passed to userspace apps */
typedef struct kapi {
    /* Version */
    uint32_t version;
    
    /* Framebuffer access */
    uint32_t *fb_base;
    int fb_width;
    int fb_height;
    int fb_pitch;
    
    /* Memory allocation */
    void *(*malloc)(size_t size);
    void (*free)(void *ptr);
    
    /* Console I/O */
    void (*putc)(char c);
    void (*puts)(const char *s);
    int (*getc)(void);
    int (*has_key)(void);
    void (*clear)(void);
    
    /* Keyboard - special keys */
    int (*get_key)(void);
    
    /* Mouse */
    void (*mouse_get_pos)(int *x, int *y);
    uint8_t (*mouse_get_buttons)(void);
    void (*mouse_get_delta)(int *dx, int *dy);
    
    /* Timing */
    uint64_t (*get_uptime_ticks)(void);
    void (*sleep_ms)(uint32_t ms);
    
    /* File I/O */
    void *(*open)(const char *path);
    void (*close)(void *handle);
    int (*read)(void *handle, void *buf, size_t count, size_t offset);
    int (*write)(void *handle, const void *buf, size_t count);
    int (*file_size)(void *handle);
    int (*create)(const char *path);
    int (*delete)(const char *path);
    int (*rename)(const char *old, const char *new);
    
    /* Process control */
    void (*exit)(int status);
    int (*exec)(const char *path);      /* Run app, wait for completion */
    int (*spawn)(const char *path);     /* Run app in background */
    void (*yield)(void);                /* Give up CPU to other tasks */
    
    /* UART for debug output */
    void (*uart_puts)(const char *s);
} kapi_t;

/* Initialize the kernel API */
void kapi_init(kapi_t *api);

/* Launch an embedded application */
typedef int (*app_main_fn)(kapi_t *api, int argc, char **argv);
int app_run(const char *name, int argc, char **argv);

#endif /* KAPI_H */
