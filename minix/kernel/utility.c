/* This file contains a collection of miscellaneous procedures:
 *   panic:    abort MINIX due to a fatal error
 *   kputc:          buffered putc used by kernel printf
 */

#include "kernel/kernel.h"
#include "arch_proto.h"

#include <minix/syslib.h>
#include <unistd.h>
#include <stdarg.h>
#include <signal.h>
#include <string.h>

#include <minix/sys_config.h>

#define ARE_PANICING 0xDEADC0FF

/*===========================================================================*
 *			panic                                          *
 *===========================================================================*/
void panic(const char *fmt, ...)
{
  va_list arg;
  /* The system has run aground of a fatal kernel error. Terminate execution. */
  if (kinfo.minix_panicing == ARE_PANICING) {
  	reset();
  }
  kinfo.minix_panicing = ARE_PANICING;
  if (fmt != NULL) {
	printf("kernel panic: ");
  	va_start(arg, fmt);
	vprintf(fmt, arg);
	va_end(arg);
	printf("\n");
  }

  printf("kernel think working on CPU %d: ", cpunr);
  util_stacktrace();


#ifdef ARCH_ARM_CORTEX_A7
    u32_t realcpuid = 0;
    asm volatile ("mrc p15, 0, %[realcpuid], c0, c0, 5 " : [realcpuid]"=r"(realcpuid));
    realcpuid &= ((1 << 2) | 1);

    printf("Realy CPU is %d \n", realcpuid);
#endif

#ifdef __arm__
   u32_t dfsr, dfar, ttbcr, ttbr0, actlr, sctlr;
   asm volatile ("mrc p15, 0, %[dfsr], c5, c0, 0" : [dfsr]"=r"(dfsr));
   asm volatile ("mrc p15, 0, %[dfar], c6, c0, 0" : [dfar]"=r"(dfar));
   asm volatile("mrc p15, 0, %[ctl], c1, c0, 0 @ Read SCTLR\n\t" : [ctl] "=r" (sctlr));
   asm volatile("mrc p15, 0, %[bar], c2, c0, 0 @ Read TTBR0\n\t" : [bar] "=r" (ttbr0));
   asm volatile("mrc p15, 0, %[ctl], c1, c0, 1 @ Read ACTLR\n\t": [ctl] "=r" (actlr));
   asm volatile("mrc p15, 0, %[bcr], c2, c0, 2 @ Read TTBCR\n\t": [bcr] "=r" (ttbcr));
   printf("DFSR: 0x%08x \n", dfsr);
   printf("DFAR: 0x%08x \n", dfar);
   printf("SCTLR: 0x%08x \n", sctlr);
   printf("ACTLR: 0x%08x \n", actlr);
   printf("TTBCR: 0x%08x \n", ttbcr);
   printf("TTBR0: 0x%08x \n", ttbr0);
#endif




#if 0
  if(get_cpulocal_var(proc_ptr)) {
	  printf("current process : ");
	  proc_stacktrace(get_cpulocal_var(proc_ptr));
  }
#endif

  /* Abort MINIX. */
  minix_shutdown(0);
}

/*===========================================================================*
 *				kputc				     	     *
 *===========================================================================*/
void kputc(
  int c					/* character to append */
)
{
/* Accumulate a single character for a kernel message. Send a notification
 * to the output drivers if an END_OF_KMESS is encountered.
 */
  if (c != END_OF_KMESS) {
      int maxblpos = sizeof(kmess.kmess_buf) - 2;
#ifdef DEBUG_SERIAL
      if (kinfo.do_serial_debug) {
	if(c == '\n')
      		ser_putc('\r');
      	ser_putc(c);
      }
#endif
      kmess.km_buf[kmess.km_next] = c;	/* put normal char in buffer */
      kmess.kmess_buf[kmess.blpos] = c;
      if (kmess.km_size < sizeof(kmess.km_buf))
          kmess.km_size += 1;
      kmess.km_next = (kmess.km_next + 1) % _KMESS_BUF_SIZE;
      if(kmess.blpos == maxblpos) {
      	memmove(kmess.kmess_buf,
		kmess.kmess_buf+1, sizeof(kmess.kmess_buf)-1);
      } else kmess.blpos++;
  } else if (!(kinfo.minix_panicing || kinfo.do_serial_debug)) {
	send_diag_sig();
  }
}

/*===========================================================================*
 *				_exit				     	     *
 *===========================================================================*/
void _exit(
  int e					/* error code */
)
{
  panic("_exit called from within the kernel, should not happen. (err %i)", e);
}
