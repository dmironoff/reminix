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

#include "omap_timer_registers.h"
#include "omap_rtc.h"

#define AM335X_CM_BASE 0x44E00000
#define AM335X_CM_SIZE 0x1000

#define AM335X_PRM_DEVICE_OFFSET 0xf00
#define AM335X_PRM_RSTCTRL_REG 0x00
#define AM335X_RST_GLOBAL_WARM_SW_BIT 0

#define DM37XX_CM_BASE 0x48307000
#define DM37XX_CM_SIZE 0x1000
#define DM37XX_PRM_RSTCTRL_REG 0x250
#define DM37XX_RST_DPLL3_BIT 2

struct omap_reset
{
	vir_bytes base;
	vir_bytes size;
};

static struct omap_reset omap_reset;

static kern_phys_map reset_phys_map;

void
bsp_reset_init(void)
{

		omap_reset.base = DM37XX_CM_BASE;
		omap_reset.size = DM37XX_CM_SIZE;


	kern_phys_map_ptr(omap_reset.base, omap_reset.size,
	    VMMF_UNCACHED | VMMF_WRITE,
	    &reset_phys_map, (vir_bytes) & omap_reset.base);
}

void
bsp_reset(void)
{

		mmio_set((omap_reset.base + DM37XX_PRM_RSTCTRL_REG),
		    (1 << DM37XX_RST_DPLL3_BIT));

}

void
bsp_poweroff(void)
{

    /* TODO: Сделать нормальное выключение у omap3 */

}

void bsp_disable_watchdog(void)
{
    /* У omap3 watchdog не используется */
}

