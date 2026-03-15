/*
 * Это маленький простейший драйвер для RTC процессора H3
 * Более полный драйвер реализован в пользовательском пространстве как
 * drivers/clock/readclock
 */

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
#include "h3_rtc.h"

struct h3_rtc
{
	vir_bytes base;
	vir_bytes size;
};

static struct h3_rtc h3_rtc = {
	.base = H3_RTC_BASE,
	.size = H3_RTC_SIZE
};

static kern_phys_map rtc_phys_map;

void
h3_rtc_init(void)
{
		kern_phys_map_ptr(h3_rtc.base, h3_rtc.size,
		    VMMF_UNCACHED | VMMF_WRITE, &rtc_phys_map,
		    (vir_bytes) & h3_rtc.base);

}

void
h3_rtc_run(void)
{
/*
 * Я не нашёл в описании чипа наличия контрольного регистра запускающего часы
 * Предполагаю, что это происходит переключением на внешний источник тактирования
 * но на Orange PI pc plus его нет, так что пока я не буду с этим морочиться
 * да и источника питания на этой плате тоже нет, а значит часы реального времени
 * останавливаются при выключении питания
 */
}
