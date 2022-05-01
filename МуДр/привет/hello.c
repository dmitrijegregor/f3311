#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/uaccess.h>
#include "find.c"

/* <linux/syscalls.h>
asmlinkage long sys_write( unsigned int fd, 
                           const char __user *buf,
                           size_t count ); */

static asmlinkage long (*sys_write) (
   unsigned int, const char __user *, size_t );

static int __init wr_init( void ) {
   char buf[ 80 ] = "Hello from kernel!\n";
   int len = strlen( buf ), n;
   sys_write = find_sym( "sys_write" );    
   printk( "+ sys_write address = %p\n", sys_write );
   printk( "+ [%d]: %s", len, buf );
   if( sys_write != NULL ) {
      mm_segment_t fs = get_fs();
      set_fs( get_ds() );
      n = sys_write( 1, buf, len );
      set_fs( fs );
      printk( "+ printf() return : %d\n", n );
   }
   return -EPERM; 
}

module_init( wr_init );
MODULE_LICENSE( "GPL" );
MODULE_AUTHOR( "Oleg Tsiliuric <olej@front.ru>" );
