/*
 * Vib-OS - Terminal Emulator
 * 
 * VT100-compatible terminal emulator for the GUI.
 */

#include "types.h"
#include "printk.h"
#include "mm/kmalloc.h"

/* Forward declare window type */
struct window;

/* External GUI functions */
extern void gui_draw_rect(int x, int y, int w, int h, uint32_t color);
extern void gui_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg);
extern struct window *gui_create_window(const char *title, int x, int y, int w, int h);

/* ===================================================================== */
/* Terminal Configuration */
/* ===================================================================== */

#define TERM_COLS       80
#define TERM_ROWS       24
#define TERM_CHAR_W     8
#define TERM_CHAR_H     16
#define TERM_PADDING    4

/* Terminal colors (VT100/ANSI) */
static const uint32_t term_colors[16] = {
    0x1E1E2E,   /* 0 - Black (background) */
    0xF38BA8,   /* 1 - Red */
    0xA6E3A1,   /* 2 - Green */
    0xF9E2AF,   /* 3 - Yellow */
    0x89B4FA,   /* 4 - Blue */
    0xCBA6F7,   /* 5 - Magenta */
    0x94E2D5,   /* 6 - Cyan */
    0xCDD6F4,   /* 7 - White (foreground) */
    0x585B70,   /* 8 - Bright Black */
    0xF38BA8,   /* 9 - Bright Red */
    0xA6E3A1,   /* 10 - Bright Green */
    0xF9E2AF,   /* 11 - Bright Yellow */
    0x89B4FA,   /* 12 - Bright Blue */
    0xCBA6F7,   /* 13 - Bright Magenta */
    0x94E2D5,   /* 14 - Bright Cyan */
    0xFFFFFF,   /* 15 - Bright White */
};

/* ===================================================================== */
/* Terminal State */
/* ===================================================================== */

struct terminal {
    /* Character buffer */
    char *chars;
    uint8_t *fg_colors;
    uint8_t *bg_colors;
    
    /* Dimensions */
    int cols;
    int rows;
    
    /* Cursor */
    int cursor_x;
    int cursor_y;
    bool cursor_visible;
    bool cursor_blink;
    
    /* Current colors */
    uint8_t current_fg;
    uint8_t current_bg;
    
    /* Escape sequence state */
    bool in_escape;
    char escape_buf[32];
    int escape_len;
    
    /* Scrollback */
    int scroll_offset;
    
    /* Associated window */
    struct window *window;
    int content_x, content_y;
    
    /* Input buffer */
    char input_buf[256];
    int input_len;
    int input_pos;
    
    /* Shell process */
    int shell_pid;
    int pty_fd;
    
    /* Current Working Directory */
    char cwd[256];
};

static struct terminal *active_terminal = NULL;

/* ===================================================================== */
/* Terminal Buffer Operations */
/* ===================================================================== */

static void term_clear_line(struct terminal *term, int row)
{
    for (int col = 0; col < term->cols; col++) {
        int idx = row * term->cols + col;
        term->chars[idx] = ' ';
        term->fg_colors[idx] = term->current_fg;
        term->bg_colors[idx] = term->current_bg;
    }
}

static void term_scroll_up(struct terminal *term)
{
    /* Move all lines up by one */
    for (int row = 0; row < term->rows - 1; row++) {
        for (int col = 0; col < term->cols; col++) {
            int src = (row + 1) * term->cols + col;
            int dst = row * term->cols + col;
            term->chars[dst] = term->chars[src];
            term->fg_colors[dst] = term->fg_colors[src];
            term->bg_colors[dst] = term->bg_colors[src];
        }
    }
    
    /* Clear last line */
    term_clear_line(term, term->rows - 1);
}

static void term_newline(struct terminal *term)
{
    term->cursor_x = 0;
    term->cursor_y++;
    
    if (term->cursor_y >= term->rows) {
        term_scroll_up(term);
        term->cursor_y = term->rows - 1;
    }
}

/* ===================================================================== */
/* Escape Sequence Processing */
/* ===================================================================== */

static void term_process_escape(struct terminal *term)
{
    if (term->escape_len < 1) return;
    
    /* CSI sequences start with [ */
    if (term->escape_buf[0] == '[') {
        char *seq = term->escape_buf + 1;
        char cmd = term->escape_buf[term->escape_len - 1];
        
        int params[8] = {0};
        int param_count = 0;
        int num = 0;
        bool in_num = false;
        
        for (int i = 0; i < term->escape_len - 1 && param_count < 8; i++) {
            char c = seq[i];
            if (c >= '0' && c <= '9') {
                num = num * 10 + (c - '0');
                in_num = true;
            } else if (c == ';') {
                if (in_num) params[param_count++] = num;
                num = 0;
                in_num = false;
            }
        }
        if (in_num) params[param_count++] = num;
        
        switch (cmd) {
            case 'A': /* Cursor Up */
                term->cursor_y -= (params[0] > 0) ? params[0] : 1;
                if (term->cursor_y < 0) term->cursor_y = 0;
                break;
                
            case 'B': /* Cursor Down */
                term->cursor_y += (params[0] > 0) ? params[0] : 1;
                if (term->cursor_y >= term->rows) term->cursor_y = term->rows - 1;
                break;
                
            case 'C': /* Cursor Forward */
                term->cursor_x += (params[0] > 0) ? params[0] : 1;
                if (term->cursor_x >= term->cols) term->cursor_x = term->cols - 1;
                break;
                
            case 'D': /* Cursor Back */
                term->cursor_x -= (params[0] > 0) ? params[0] : 1;
                if (term->cursor_x < 0) term->cursor_x = 0;
                break;
                
            case 'H': /* Cursor Position */
            case 'f':
                term->cursor_y = (params[0] > 0) ? params[0] - 1 : 0;
                term->cursor_x = (param_count > 1 && params[1] > 0) ? params[1] - 1 : 0;
                if (term->cursor_y >= term->rows) term->cursor_y = term->rows - 1;
                if (term->cursor_x >= term->cols) term->cursor_x = term->cols - 1;
                break;
                
            case 'J': /* Erase Display */
                if (params[0] == 2) {
                    /* Clear entire screen */
                    for (int row = 0; row < term->rows; row++) {
                        term_clear_line(term, row);
                    }
                    term->cursor_x = 0;
                    term->cursor_y = 0;
                }
                break;
                
            case 'K': /* Erase Line */
                for (int col = term->cursor_x; col < term->cols; col++) {
                    int idx = term->cursor_y * term->cols + col;
                    term->chars[idx] = ' ';
                }
                break;
                
            case 'm': /* SGR - Select Graphic Rendition */
                for (int i = 0; i < param_count; i++) {
                    int p = params[i];
                    if (p == 0) {
                        term->current_fg = 7;
                        term->current_bg = 0;
                    } else if (p >= 30 && p <= 37) {
                        term->current_fg = p - 30;
                    } else if (p >= 40 && p <= 47) {
                        term->current_bg = p - 40;
                    } else if (p >= 90 && p <= 97) {
                        term->current_fg = p - 90 + 8;
                    } else if (p >= 100 && p <= 107) {
                        term->current_bg = p - 100 + 8;
                    }
                }
                break;
        }
    }
    
    term->in_escape = false;
    term->escape_len = 0;
}

/* ===================================================================== */
/* Character Output */
/* ===================================================================== */

void term_putc(struct terminal *term, char c)
{
    if (term->in_escape) {
        term->escape_buf[term->escape_len++] = c;
        
        /* Check for end of escape sequence */
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '~') {
            term_process_escape(term);
        } else if (term->escape_len >= 31) {
            term->in_escape = false;
            term->escape_len = 0;
        }
        return;
    }
    
    switch (c) {
        case '\033': /* ESC */
            term->in_escape = true;
            term->escape_len = 0;
            break;
            
        case '\n':
            term_newline(term);
            break;
            
        case '\r':
            term->cursor_x = 0;
            break;
            
        case '\b':
            if (term->cursor_x > 0) {
                term->cursor_x--;
            }
            break;
            
        case '\t':
            term->cursor_x = (term->cursor_x + 8) & ~7;
            if (term->cursor_x >= term->cols) {
                term_newline(term);
            }
            break;
            
        default:
            if (c >= 32 && c < 127) {
                int idx = term->cursor_y * term->cols + term->cursor_x;
                term->chars[idx] = c;
                term->fg_colors[idx] = term->current_fg;
                term->bg_colors[idx] = term->current_bg;
                
                term->cursor_x++;
                if (term->cursor_x >= term->cols) {
                    term_newline(term);
                }
            }
            break;
    }
}

void term_puts(struct terminal *term, const char *str)
{
    while (*str) {
        term_putc(term, *str++);
    }
}

/* ===================================================================== */
/* Rendering */
/* ===================================================================== */

void term_render(struct terminal *term)
{
    if (!term) return;
    
    int base_x = term->content_x + TERM_PADDING;
    int base_y = term->content_y + TERM_PADDING;
    
    /* Draw background */
    gui_draw_rect(term->content_x, term->content_y,
                 term->cols * TERM_CHAR_W + TERM_PADDING * 2,
                 term->rows * TERM_CHAR_H + TERM_PADDING * 2,
                 term_colors[0]);
    
    /* Draw characters */
    for (int row = 0; row < term->rows; row++) {
        for (int col = 0; col < term->cols; col++) {
            int idx = row * term->cols + col;
            char c = term->chars[idx];
            uint32_t fg = term_colors[term->fg_colors[idx] & 0xF];
            uint32_t bg = term_colors[term->bg_colors[idx] & 0xF];
            
            int x = base_x + col * TERM_CHAR_W;
            int y = base_y + row * TERM_CHAR_H;
            
            gui_draw_char(x, y, c, fg, bg);
        }
    }
    
    /* Draw cursor */
    if (term->cursor_visible) {
        int x = base_x + term->cursor_x * TERM_CHAR_W;
        int y = base_y + term->cursor_y * TERM_CHAR_H;
        gui_draw_rect(x, y, TERM_CHAR_W, TERM_CHAR_H, term_colors[7]);
    }
}

/* ===================================================================== */
/* Shell Command Execution */
/* ===================================================================== */

static int str_starts_with(const char *str, const char *prefix)
{
    while (*prefix) {
        if (*str++ != *prefix++) return 0;
    }
    return 1;
}

#include "fs/vfs.h"

/* Helper for ls command */
static int ls_callback(void *ctx, const char *name, int len, loff_t offset, ino_t ino, unsigned type)
{
    struct terminal *term = (struct terminal *)ctx;
    
    char buf[256];
    int i;
    for (i = 0; i < len && i < 255; i++) buf[i] = name[i];
    buf[i] = '\0';
    
    /* Type >> 12. 4 = DIR, 8 = REG */
    /* Check if directory */
    if (type == 4) {
        term_puts(term, "\033[1;34m"); /* Bright Blue */
        term_puts(term, buf);
        term_puts(term, "/\033[0m  ");
    } else {
        term_puts(term, buf);
        term_puts(term, "  ");
    }
    return 0;
}

static void term_execute_command(struct terminal *term, const char *cmd)
{
    /* Skip leading whitespace */
    while (*cmd == ' ') cmd++;
    
    if (*cmd == '\0') return;
    
    /* Built-in commands */
    if (str_starts_with(cmd, "clear")) {
        for (int row = 0; row < term->rows; row++) {
            term_clear_line(term, row);
        }
        term->cursor_x = 0;
        term->cursor_y = 0;
    }
    else if (str_starts_with(cmd, "help")) {
        term_puts(term, "\033[1;36mVib-OS Terminal v1.0\033[0m\n");
        term_puts(term, "\033[33mBuilt-in Commands:\033[0m\n");
        term_puts(term, "  help      - Show this help message\n");
        term_puts(term, "  clear     - Clear the terminal screen\n");
        term_puts(term, "  ls        - List directory contents (Real VFS)\n");
        term_puts(term, "  pwd       - Print working directory\n");
        term_puts(term, "  cd <dir>  - Change directory\n");
        term_puts(term, "  cat <f>   - Display file contents\n");
        term_puts(term, "  browser   - Launch Web Browser\n");
        term_puts(term, "  ping <ip> - Ping remote host\n");
        term_puts(term, "  sound     - Test audio output\n");
        term_puts(term, "  neofetch  - System info\n");
    }
    else if (str_starts_with(cmd, "ls")) {
        const char *path = term->cwd[0] ? term->cwd : "/";
        struct file *dir = vfs_open(path, O_RDONLY, 0);
        if (dir) {
            vfs_readdir(dir, term, ls_callback);
            vfs_close(dir);
            term_puts(term, "\n");
        } else {
            term_puts(term, "ls: Failed to open root directory\n");
        }
    }
    else if (str_starts_with(cmd, "pwd")) {
        if (term->cwd[0])
            term_puts(term, term->cwd);
        else
            term_puts(term, "/");
        term_puts(term, "\n");
    }
    else if (str_starts_with(cmd, "cd ")) {
        char *path = (char*)cmd + 3;
        while (*path == ' ') path++;
        
        /* Remove newline if present */
        int len = 0; while(path[len] && path[len] != '\n') len++;
        path[len] = '\0';
        
        if (len == 0) return;
        
        /* Handle relative paths manually for now or use vfs_lookup if absolute */
        char target[256];
        if (path[0] == '/') {
            int i=0; while(path[i] && i<255) { target[i]=path[i]; i++; } target[i]='\0';
        } else {
            /* Append to CWD */
            int i=0; while(term->cwd[i]) { target[i]=term->cwd[i]; i++; }
            if (i > 0 && target[i-1] != '/') target[i++] = '/';
            int j=0; while(path[j] && i<255) { target[i++] = path[j++]; }
            target[i] = '\0';
        }
        
        /* Verify path exists and is dir */
        struct file *dir = vfs_open(target, O_RDONLY, 0);
        if (dir) {
            /* Success */
            int i=0; while(target[i]) { term->cwd[i]=target[i]; i++; } term->cwd[i]='\0';
            vfs_close(dir);
        } else {
            term_puts(term, "cd: No such directory: ");
            term_puts(term, path);
            term_puts(term, "\n");
        }
    }
    else if (str_starts_with(cmd, "cat")) {
        term_puts(term, "cat: No such file or directory\n");
    }
    else if (str_starts_with(cmd, "echo ")) {
        term_puts(term, cmd + 5);
        term_puts(term, "\n");
    }
    else if (str_starts_with(cmd, "uname")) {
        term_puts(term, "Vib-OS 0.5.0 ARM64 aarch64\n");
    }
    else if (str_starts_with(cmd, "date")) {
        term_puts(term, "Thu Jan 16 21:35:00 EST 2026\n");
    }
    else if (str_starts_with(cmd, "uptime")) {
        term_puts(term, " 21:35:00 up 0 min,  1 user,  load: 0.00, 0.00, 0.00\n");
    }
    else if (str_starts_with(cmd, "free")) {
        term_puts(term, "              total        used        free\n");
        term_puts(term, "Mem:         252 MB       12 MB      240 MB\n");
        term_puts(term, "Swap:          0 MB        0 MB        0 MB\n");
    }
    else if (str_starts_with(cmd, "ps")) {
        term_puts(term, "  PID TTY          TIME CMD\n");
        term_puts(term, "    1 ?        00:00:00 init\n");
        term_puts(term, "    2 ?        00:00:00 kthread\n");
        term_puts(term, "   10 tty1     00:00:00 shell\n");
    }
    else if (str_starts_with(cmd, "whoami")) {
        term_puts(term, "root\n");
    }
    else if (str_starts_with(cmd, "neofetch")) {
        term_puts(term, "\033[36m");
        term_puts(term, "       _   _       _       ___  ____  \n");
        term_puts(term, "      | | | |_ __ (_)_  __/ _ \\/ ___| \n");
        term_puts(term, "      | | | | '_ \\| \\ \\/ / | | \\___ \\ \n");
        term_puts(term, "      | |_| | | | | |>  <| |_| |___) |\n");
        term_puts(term, "       \\___/|_| |_|_/_/\\_\\___/|____/ \n");
        term_puts(term, "\033[0m\n");
        term_puts(term, "\033[33mOS:\033[0m      Vib-OS 0.5.0\n");
        term_puts(term, "\033[33mHost:\033[0m    QEMU ARM Virtual Machine\n");
        term_puts(term, "\033[33mKernel:\033[0m  0.5.0-arm64\n");
        term_puts(term, "\033[33mUptime:\033[0m  0 mins\n");
        term_puts(term, "\033[33mShell:\033[0m   vsh 1.0\n");
        term_puts(term, "\033[33mMemory:\033[0m  12 MB / 252 MB\n");
        term_puts(term, "\033[33mCPU:\033[0m     ARM Cortex-A72 (max)\n");
        term_puts(term, "\033[33mGPU:\033[0m     QEMU ramfb 1024x768\n");
    }
    else if (str_starts_with(cmd, "exit")) {
        term_puts(term, "\033[33mGoodbye!\033[0m\n");
    }
    else if (str_starts_with(cmd, "sound")) {
        term_puts(term, "Playing test tone (440Hz Square Wave)...\n");
        
        extern int intel_hda_play_pcm(const void *data, uint32_t samples, uint8_t channels, uint32_t sample_rate);
        
        uint32_t samples = 48000; /* 1 second */
        int16_t *buf = (int16_t *)kmalloc(samples * 4);
        if (buf) {
            for (uint32_t i = 0; i < samples; i++) {
                int16_t val = (i % 100) < 50 ? 8000 : -8000;
                buf[i*2] = val;
                buf[i*2+1] = val;
            }
            intel_hda_play_pcm(buf, samples, 2, 48000);
            /* Don't free immediately as DMA keeps using it, slight leak for test is fine or we need a callback */
        } else {
            term_puts(term, "Error: memory allocation failed\n");
        }
    }
    else if (str_starts_with(cmd, "ping ")) {
        term_puts(term, "Pinging ");
        term_puts(term, cmd + 5);
        term_puts(term, "...\n");
        
        char *ip_str = (char*)cmd + 5;
        uint32_t ip = 0;
        int octet = 0;
        int shift = 24;
        
        while (*ip_str) {
            if (*ip_str == '.') {
                ip |= (octet << shift);
                shift -= 8;
                octet = 0;
            } else if (*ip_str >= '0' && *ip_str <= '9') {
                octet = octet * 10 + (*ip_str - '0');
            }
            ip_str++;
        }
        ip |= (octet << shift);
        
        /* 0x0A000202 */
        // term_printf("IP: %08x\n", ip);
        
        extern int icmp_send_echo(uint32_t dest_ip, uint16_t id, uint16_t seq);
        icmp_send_echo(ip, 1, 1);
        term_puts(term, "Packet sent.\n");
    }
    else if (str_starts_with(cmd, "browser")) {
        term_puts(term, "Starting Browser...\n");
        gui_create_window("Browser", 150, 100, 600, 450);
    }
    else {
        term_puts(term, "\033[31mCommand not found:\033[0m ");
        term_puts(term, cmd);
        term_puts(term, "\nType 'help' for available commands.\n");
    }
}

/* ===================================================================== */
/* Input Handling */
/* ===================================================================== */

void term_handle_key(struct terminal *term, int key)
{
    if (!term) return;
    
    if (key == '\n' || key == '\r') {
        /* Process command */
        term->input_buf[term->input_len] = '\0';
        term_putc(term, '\n');
        
        /* Execute command */
        if (term->input_len > 0) {
            term_execute_command(term, term->input_buf);
        }
        
        /* Show new prompt */
        term_puts(term, "\033[32mvib-os\033[0m:\033[34m~\033[0m$ ");
        
        term->input_len = 0;
        term->input_pos = 0;
    } else if (key == '\b' || key == 127) {
        if (term->input_len > 0) {
            term->input_len--;
            term->cursor_x--;
            int idx = term->cursor_y * term->cols + term->cursor_x;
            term->chars[idx] = ' ';
        }
    } else if (key >= 32 && key < 127) {
        if (term->input_len < 255) {
            term->input_buf[term->input_len++] = key;
            term_putc(term, key);
        }
    }
}

/* ===================================================================== */
/* Terminal Creation */
/* ===================================================================== */

struct terminal *term_create(int x, int y, int cols, int rows)
{
    struct terminal *term = kmalloc(sizeof(struct terminal));
    if (!term) return NULL;
    
    term->cols = cols;
    term->rows = rows;
    
    size_t buf_size = cols * rows;
    term->chars = kmalloc(buf_size);
    term->fg_colors = kmalloc(buf_size);
    term->bg_colors = kmalloc(buf_size);
    
    if (!term->chars || !term->fg_colors || !term->bg_colors) {
        if (term->chars) kfree(term->chars);
        if (term->fg_colors) kfree(term->fg_colors);
        if (term->bg_colors) kfree(term->bg_colors);
        kfree(term);
        return NULL;
    }
    
    /* Initialize */
    term->cursor_x = 0;
    term->cursor_y = 0;
    term->cursor_visible = true;
    term->current_fg = 7;
    term->current_bg = 0;
    term->in_escape = false;
    term->escape_len = 0;
    term->input_len = 0;
    term->input_pos = 0;
    term->content_x = x;
    term->content_y = y;
    
    /* Init CWD */
    term->cwd[0] = '/';
    term->cwd[1] = '\0';
    
    /* Clear buffer */
    for (int row = 0; row < rows; row++) {
        term_clear_line(term, row);
    }
    
    /* Print welcome message */
    term_puts(term, "\033[1;36mVib-OS Terminal v1.0\033[0m\n");
    term_puts(term, "Type '\033[33mhelp\033[0m' for commands, '\033[33mneofetch\033[0m' for system info.\n\n");
    term_puts(term, "\033[32mvib-os\033[0m:\033[34m~\033[0m$ ");
    
    printk(KERN_INFO "TERM: Created terminal %dx%d\n", cols, rows);
    
    return term;
}

void term_destroy(struct terminal *term)
{
    if (!term) return;
    
    if (term->chars) kfree(term->chars);
    if (term->fg_colors) kfree(term->fg_colors);
    if (term->bg_colors) kfree(term->bg_colors);
    kfree(term);
}

struct terminal *term_get_active(void)
{
    return active_terminal;
}

void term_set_active(struct terminal *term)
{
    active_terminal = term;
}
