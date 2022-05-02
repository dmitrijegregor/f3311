// Normal LKM stuff
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/syscalls.h>

// __NR_* goodness
#include <linux/unistd.h>

// reading files
#include <linux/fs.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>

// SYS_CALL_TABLE_ADDRESS - this will be filled via the makefile
#define SYS_CALL_TABLE_ADDRESS 0xc000f764
#define SYS_CALL_printk 0xc09a46e4

// No longer exported, could actually enable this in the kernel though...
//extern void* sys_call_table[];
void **sys_call_table;

int (*original_open)(const char* path, int flags, mode_t mode);
size_t (*original_write)(int fildes, const char *buf, size_t nbytes);
ssize_t (*original_read)(int fd, char *buf, size_t count);
uid_t (*original_getuid)(void);
pid_t (*original_getpid)(void);
//define printk(fmt, ...) printk_index_wrap(_printk, fmt, ##__VA_ARGS__)
void (*mu_printk)(const char *fmt , ... );
