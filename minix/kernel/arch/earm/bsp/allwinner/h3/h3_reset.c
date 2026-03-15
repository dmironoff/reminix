#include <assert.h>
#include <sys/types.h>
#include <machine/cpu.h>
#include <minix/type.h>
#include <minix/board.h>
#include <io.h>

#include "kernel/kernel.h"
#include "kernel/proc.h"
#include "kernel/vm.h"
#include "kernel/proto.h"
#include "arch_proto.h"
#include "bsp_reset.h"

#include "h3_timer_registers.h"
#include "h3_rtc.h"


void
bsp_reset_init(void)
{

}

void
bsp_reset(void)
{

}

void
bsp_poweroff(void)
{


}

void bsp_disable_watchdog(void)
{
/*
 * У меня огромные вопросы что это говно делает именно здесь
 * но в реализации у создетелй системы в bsp для BeagleBone
 * это говно было здесь, а я слизал весь этот уровень у них
 */
}

