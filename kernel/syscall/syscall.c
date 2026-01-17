/*
 * UnixOS Kernel - System Call Implementation
 */

#include "syscall/syscall.h"
#include "sched/sched.h"
#include "fs/vfs.h"
#include "printk.h"
#include "drivers/uart.h"

/* ===================================================================== */
/* System call table */
/* ===================================================================== */

typedef long (*syscall_fn_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

static syscall_fn_t syscall_table[NR_syscalls];

/* ===================================================================== */
/* System call implementations */
/* ===================================================================== */

static long sys_read(uint64_t fd, uint64_t buf, uint64_t count, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a3; (void)a4; (void)a5;
    
    /* TODO: Get file from fd table */
    (void)fd;
    (void)buf;
    (void)count;
    
    return -ENOSYS;
}

static long sys_write(uint64_t fd, uint64_t buf, uint64_t count, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a3; (void)a4; (void)a5;
    
    /* Special case: stdout/stderr (fd 1 and 2) go to console */
    if (fd == 1 || fd == 2) {
        const char *str = (const char *)buf;
        for (size_t i = 0; i < count; i++) {
            uart_putc(str[i]);
        }
        return count;
    }
    
    return -EBADF;
}

static long sys_openat(uint64_t dirfd, uint64_t pathname, uint64_t flags, uint64_t mode, uint64_t a4, uint64_t a5)
{
    (void)a4; (void)a5;
    (void)dirfd;
    
    const char *path = (const char *)pathname;
    printk(KERN_DEBUG "sys_openat: '%s' flags=0x%llx mode=0%llo\n", path, (unsigned long long)flags, (unsigned long long)mode);
    
    return -ENOSYS;
}

static long sys_close(uint64_t fd, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    (void)fd;
    
    return -ENOSYS;
}

static long sys_lseek(uint64_t fd, uint64_t offset, uint64_t whence, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a3; (void)a4; (void)a5;
    (void)fd; (void)offset; (void)whence;
    
    return -ENOSYS;
}

static long sys_exit(uint64_t error_code, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    
    printk(KERN_INFO "sys_exit: code=%llu\n", (unsigned long long)error_code);
    exit_task((int)error_code);
    
    /* Never reached */
    return 0;
}

static long sys_exit_group(uint64_t error_code, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    return sys_exit(error_code, a1, a2, a3, a4, a5);
}

static long sys_getpid(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    
    struct task_struct *current = get_current();
    return current ? current->pid : -1;
}

static long sys_getppid(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    
    struct task_struct *current = get_current();
    if (current && current->parent) {
        return current->parent->pid;
    }
    return 0;
}

static long sys_getuid(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    
    struct task_struct *current = get_current();
    return current ? current->uid : 0;
}

static long sys_getgid(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    
    struct task_struct *current = get_current();
    return current ? current->gid : 0;
}

static long sys_gettid(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    
    struct task_struct *current = get_current();
    return current ? current->pid : -1;
}

/* Userspace heap management - dedicated region for userspace processes */
#define USER_HEAP_START 0x10000000UL  /* 256MB mark */
#define USER_HEAP_SIZE  0x04000000UL  /* 64MB heap */
static uint64_t user_brk_current = USER_HEAP_START;
static uint64_t user_mmap_current = USER_HEAP_START + USER_HEAP_SIZE / 2;  /* mmap from middle */

static long sys_brk(uint64_t brk, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    
    /* If brk is 0 or less than start, return current brk */
    if (brk == 0 || brk < USER_HEAP_START) {
        return user_brk_current;
    }
    
    /* Check bounds */
    if (brk > USER_HEAP_START + USER_HEAP_SIZE / 2) {
        /* Would overlap with mmap region */
        return user_brk_current;
    }
    
    /* Extend brk */
    user_brk_current = brk;
    return user_brk_current;
}

static long sys_mmap(uint64_t addr, uint64_t len, uint64_t prot, uint64_t flags, uint64_t fd, uint64_t offset)
{
    (void)addr; (void)prot; (void)offset;
    
    /* Only support anonymous mappings for now */
    #define MAP_ANONYMOUS 0x20
    if (!(flags & MAP_ANONYMOUS) || (int64_t)fd != -1) {
        printk(KERN_DEBUG "sys_mmap: only anonymous mappings supported\n");
        return -ENOSYS;
    }
    
    /* Align len to page size */
    #define PAGE_SIZE 4096UL
    len = (len + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    /* Check bounds */
    if (user_mmap_current + len > USER_HEAP_START + USER_HEAP_SIZE) {
        printk(KERN_WARNING "sys_mmap: out of memory\n");
        return -ENOMEM;
    }
    
    /* Allocate from mmap region */
    uint64_t result = user_mmap_current;
    user_mmap_current += len;
    
    /* Zero the memory */
    uint8_t *p = (uint8_t *)result;
    for (size_t i = 0; i < len; i++) p[i] = 0;
    
    return result;
}

static long sys_munmap(uint64_t addr, uint64_t len, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)addr; (void)len; (void)a2; (void)a3; (void)a4; (void)a5;
    
    /* For now, just no-op munmap - memory is not reclaimed */
    return 0;
}

static long sys_clone(uint64_t flags, uint64_t stack, uint64_t ptid, uint64_t tls, uint64_t ctid, uint64_t a5)
{
    (void)flags; (void)stack; (void)ptid; (void)tls; (void)ctid; (void)a5;
    
    /* TODO: Implement process/thread creation */
    
    return -ENOSYS;
}

static long sys_execve(uint64_t filename, uint64_t argv, uint64_t envp, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)filename; (void)argv; (void)envp; (void)a3; (void)a4; (void)a5;
    
    const char *path = (const char *)filename;
    printk(KERN_DEBUG "sys_execve: '%s'\n", path);
    
    /* TODO: Implement program loading */
    
    return -ENOSYS;
}

static long sys_uname(uint64_t buf, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    
    struct utsname {
        char sysname[65];
        char nodename[65];
        char release[65];
        char version[65];
        char machine[65];
        char domainname[65];
    };
    
    struct utsname *uts = (struct utsname *)buf;
    
    /* Copy strings (simple implementation) */
    const char *sysname = "UnixOS";
    const char *nodename = "localhost";
    const char *release = "0.1.0";
    const char *version = "0.1.0-arm64";
    const char *machine = "aarch64";
    const char *domain = "";
    
    for (int i = 0; i < 64 && sysname[i]; i++) uts->sysname[i] = sysname[i];
    uts->sysname[64] = 0;
    for (int i = 0; i < 64 && nodename[i]; i++) uts->nodename[i] = nodename[i];
    uts->nodename[64] = 0;
    for (int i = 0; i < 64 && release[i]; i++) uts->release[i] = release[i];
    uts->release[64] = 0;
    for (int i = 0; i < 64 && version[i]; i++) uts->version[i] = version[i];
    uts->version[64] = 0;
    for (int i = 0; i < 64 && machine[i]; i++) uts->machine[i] = machine[i];
    uts->machine[64] = 0;
    for (int i = 0; i < 64 && domain[i]; i++) uts->domainname[i] = domain[i];
    uts->domainname[64] = 0;
    
    return 0;
}

static long sys_sched_yield(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    
    schedule();
    return 0;
}

static long sys_nanosleep(uint64_t req, uint64_t rem, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)rem; (void)a2; (void)a3; (void)a4; (void)a5;
    
    /* TODO: Implement proper sleep */
    (void)req;
    
    return 0;
}

static long sys_not_implemented(uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    return -ENOSYS;
}

/* ===================================================================== */
/* Syscall initialization */
/* ===================================================================== */

void syscall_init(void)
{
    printk(KERN_INFO "SYSCALL: Initializing system call table\n");
    
    /* Initialize all to not implemented */
    for (int i = 0; i < NR_syscalls; i++) {
        syscall_table[i] = sys_not_implemented;
    }
    
    /* Register implemented syscalls */
    syscall_table[SYS_read] = sys_read;
    syscall_table[SYS_write] = sys_write;
    syscall_table[SYS_openat] = sys_openat;
    syscall_table[SYS_close] = sys_close;
    syscall_table[SYS_lseek] = sys_lseek;
    syscall_table[SYS_exit] = sys_exit;
    syscall_table[SYS_exit_group] = sys_exit_group;
    syscall_table[SYS_getpid] = sys_getpid;
    syscall_table[SYS_getppid] = sys_getppid;
    syscall_table[SYS_getuid] = sys_getuid;
    syscall_table[SYS_geteuid] = sys_getuid;
    syscall_table[SYS_getgid] = sys_getgid;
    syscall_table[SYS_getegid] = sys_getgid;
    syscall_table[SYS_gettid] = sys_gettid;
    syscall_table[SYS_brk] = sys_brk;
    syscall_table[SYS_mmap] = sys_mmap;
    syscall_table[SYS_munmap] = sys_munmap;
    syscall_table[SYS_clone] = sys_clone;
    syscall_table[SYS_execve] = sys_execve;
    syscall_table[SYS_uname] = sys_uname;
    syscall_table[SYS_sched_yield] = sys_sched_yield;
    syscall_table[SYS_nanosleep] = sys_nanosleep;
    
    printk(KERN_INFO "SYSCALL: System call table initialized\n");
}

/* ===================================================================== */
/* Syscall dispatcher */
/* ===================================================================== */

long handle_syscall(struct pt_regs *regs)
{
    /* ARM64 syscall convention:
     * x8 = syscall number
     * x0-x5 = arguments
     * x0 = return value
     */
    
    uint64_t nr = regs->regs[8];
    
    if (nr >= NR_syscalls) {
        printk(KERN_WARNING "SYSCALL: Invalid syscall number %llu\n", (unsigned long long)nr);
        return -ENOSYS;
    }
    
    syscall_fn_t fn = syscall_table[nr];
    
    return fn(regs->regs[0], regs->regs[1], regs->regs[2],
              regs->regs[3], regs->regs[4], regs->regs[5]);
}

/* ===================================================================== */
/* Exception handler */
/* ===================================================================== */

void handle_sync_exception(struct pt_regs *regs)
{
    /* Read exception syndrome register */
    uint64_t esr;
    asm volatile("mrs %0, esr_el1" : "=r" (esr));
    
    uint32_t ec = (esr >> 26) & 0x3F;  /* Exception class */
    uint32_t iss = esr & 0x1FFFFFF;    /* Instruction specific syndrome */
    
    switch (ec) {
        case 0x15:  /* SVC instruction from AArch64 */
            /* System call - handled separately */
            break;
            
        case 0x20:  /* Instruction abort from lower EL */
        case 0x21:  /* Instruction abort from same EL */
            printk(KERN_EMERG "Instruction abort at PC=0x%llx\n", (unsigned long long)regs->pc);
            panic("Instruction abort");
            break;
            
        case 0x24:  /* Data abort from lower EL */
        case 0x25:  /* Data abort from same EL */
            {
                uint64_t far;
                asm volatile("mrs %0, far_el1" : "=r" (far));
                printk(KERN_EMERG "Data abort at PC=0x%llx, FAR=0x%llx\n", (unsigned long long)regs->pc, (unsigned long long)far);
                panic("Data abort");
            }
            break;
            
        case 0x00:  /* Unknown reason */
            printk(KERN_EMERG "Unknown exception at PC=0x%llx\n", (unsigned long long)regs->pc);
            panic("Unknown exception");
            break;
            
        default:
            printk(KERN_EMERG "Unhandled exception class 0x%x, ISS=0x%x\n", ec, iss);
            printk(KERN_EMERG "PC=0x%llx\n", (unsigned long long)regs->pc);
            panic("Unhandled exception");
            break;
    }
}
