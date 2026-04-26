/* memory.c — ReMinix ARM32 memory operations.
 *
 * Ключевые изменения относительно оригинала:
 *  - freepdes[] инициализируется из BKI->vir_memory_cp_region_addr
 *    (два последовательных L1-слота в области межпроцессного копирования).
 *  - vm_lookup() ходит сначала в apt, только потом в физическую таблицу.
 *  - switch_address_space() использует rp->pt_handler для pg_load_ttbr0().
 *  - p_ttbr / p_ttbr_v сохранены для совместимости с assert'ами и mpx.S.
 */

#include "kernel/kernel.h"
#include "kernel/proc.h"
#include "kernel/vm.h"

#include <machine/vm.h>
#include <minix/type.h>
#include <minix/board.h>
#include <minix/syslib.h>
#include <minix/cpufeature.h>
#include <string.h>
#include <assert.h>
#include <signal.h>
#include <stdlib.h>

#include "arch_proto.h"
#include "kernel/proto.h"
#include "kernel/debug.h"
#include "bsp_timer.h"
#include "pagetables.h"
#include "kernel/bootstrap_kernel_information.h"
#include "kernel/apt_utils.h"
#include "kernel/mmap_utils.h"

extern vm_abstract_pagetables_t       *apt; // Абстрактная таблица страниц
extern vir_bytes                       arch_pt_base; // Базовый адрес физической аппаратной таблицы страниц
extern mmap_t                         *mmap; // Абстрактная карта памяти
extern struct kinfo                   kinfo;

/*
 * Проверяем наличие у процесса своей таблицы страниц
 */
#define HASPT(procptr) ((procptr)->pt_handler != 0xFFFFFFFF)

static int nfreepdes = 0;
#define MAXFREEPDES 2
static int freepdes[MAXFREEPDES];   /* индексы L1 в текущей таблице ядра */

static u32_t phys_get32(phys_bytes v);


/*
 * вычисление оффсета для выравнивая l1 относительно выделенного региона
 */
static inline uint32_t pdoff(uint32_t sector_start) {
    return (sector_start + (ARM_PD_ALIGN - 1)) & ~(ARM_PD_ALIGN - 1) - sector_start;
}

/*
 * Вычисление оффсета начала таблицы l2 относительно начала выделенного региона
 */
static inline uint32_t ptoff(uint32_t sector_start) {
    return ptaddr(sector_start) + ARM_PD_SIZE;
}


/*===========================================================================*
 *                          memory_init                                      *
 *===========================================================================*/
void memory_init(void)
{
    assert(nfreepdes == 0);
    assert(BKI);

    /* Берём два последовательных L1-слота из области копирования.
     * В pre_init эта область размечена как VM_APF_USER_TO_KERNEL_CP_SPACE
     * и занимает как минимум 2 * ARM_L1_SIZE байт.                         */
    freepdes[nfreepdes++] = ARM_L1_INDEX(kinfo->vir_memory_cp_region_addr);
    freepdes[nfreepdes++] = ARM_L1_INDEX(kinfo->vir_memory_cp_region_addr
                                         + ARM_L1_SIZE);

    assert(nfreepdes == MAXFREEPDES);
}

/*
 * Сброс записей о наших окнах копирования в физической таблице текущего процесса
 */
void mem_clear_mapcache(void)
{
    int i;
    struct proc *ptproc = get_cpulocal_var(ptproc);
    mmap_region_t *my_pd_reg = (mmap_region_t *) ((arm_pt_t *) kinfo->arch_pagetables)[ptproc->pt_handler].table_region;
    uint32_t *ptbase = (uint32_t *) (kinfo->vir_memory_pt_region_addr + pdoff(my_pd_reg->start));
    for (i = 0; i < nfreepdes; i++) {
        ptbase[freepdes[i]] = 0;
        // Сбросим эту запись в кешах процессоров
        tlbi_mva_asid_is(freepdes[i] * ARM_L1_SIZE, ptproc->context_id.id);
    }
}

/*
 * Создание окна в память другого процесса
 */
static phys_bytes createpde(
        const struct proc *pr,
        const phys_bytes   linaddr,
        phys_bytes        *bytes,
        int                free_pde_idx)
{
    u32_t pdeval;
    phys_bytes offset;
    int pde;
    mmap_region_t *my_pd_reg = (mmap_region_t *) ((arm_pt_t *) kinfo->arch_pagetables)[get_cpulocal_var(ptproc)->pt_handler].table_region;

    assert(free_pde_idx >= 0 && free_pde_idx < nfreepdes);
    pde = freepdes[free_pde_idx];
    assert(pde >= 0 && pde < ARM_L1_ENTRIES);

    // Процесс ядерный, так что там нет своей таблицы - возвращаем линейный адрес
    if (pr && ((pr == get_cpulocal_var(ptproc)) || iskernelp(pr)))
        return linaddr;

    if (pr) {
        // Наш процесс сейчас не запущен, так что мы просто покопаемся в его таблице страниц
        // И сопрём данные записи нужного нам участка, точнее секции 1MB
        map_pt_table_to_me(pr->pt_handler);
        mmap_region_t *reg = ((arm_pt_t *) arch_pt_base)[pr->pt_handler].table_region;
        uint32_t *ptbase = (uint32_t *) (kinfo->vir_memory_pt_work_region_addr + pdoff(reg->start));;
        pdeval = ptbase[ARM_L1_INDEX(linaddr)];
        unmap_pt_table_to_me();
    } else {
        // Это вообще не процесс, так что просто мапим физическую память.
        // Думал написать проверку на физическую память через MMAP, но решил что это будет тормозить систему
        // Если что, то потом.
        // В оригинале если пихнуть в эту функцию адрес устройства, то будет паника.
        pdeval = (linaddr & ARM_L1_ADDR_MASK)
                 | ARM_L1_TYPE_SECTION
                 | ARM_L1_DOMAIN(0)
                 | ARM_L1_WRITE_THROUGH
                 | ARM_L1_AP_KRW_URW;
    }


    uint32_t *ptbase = (uint32_t *) (kinfo->vir_memory_pt_region_addr + pdoff(my_pd_reg->start));
    if (ptbase[pde] != pdeval) {
        // Если наша новая запись ещё не такая как нужно, то мы ее делаем такой и сбрасываем опять таки кеш для неё
        ptbase[pde] = pdeval;
        tlbi_mva_asid_is(pde * ARM_L1_SIZE, get_cpulocal_var(ptproc)->context_id.id);
    }

    offset  = linaddr & ARM_VM_OFFSET_MASK_1MB;
    *bytes  = MIN(*bytes, ARM_SECTION_SIZE - offset);
    return ARM_SECTION_SIZE * pde + offset;
}

/*===========================================================================*
 *                      check_resumed_caller                                 *
 *===========================================================================*/
static int check_resumed_caller(struct proc *caller)
{
    if (caller && (caller->p_misc_flags & MF_KCALL_RESUME)) {
        assert(caller->p_vmrequest.vmresult != VMSUSPEND);
        return caller->p_vmrequest.vmresult;
    }
    return OK;
}

/*===========================================================================*
 *                          lin_lin_copy                                     *
 *===========================================================================*/
static int lin_lin_copy(struct proc *srcproc, vir_bytes srclinaddr,
                        struct proc *dstproc, vir_bytes dstlinaddr,
                        vir_bytes bytes)
{
    u32_t addr;
    proc_nr_t procslot;

    assert(get_cpulocal_var(ptproc));
    assert(get_cpulocal_var(proc_ptr));
    assert(read_ttbr0() == get_cpulocal_var(ptproc)->p_seg.p_ttbr);

    procslot = get_cpulocal_var(ptproc)->p_nr;
    assert(procslot >= 0 && procslot < ARM_VM_DIR_ENTRIES);

    if (srcproc) assert(!RTS_ISSET(srcproc, RTS_SLOT_FREE));
    if (dstproc) assert(!RTS_ISSET(dstproc, RTS_SLOT_FREE));
    assert(!RTS_ISSET(get_cpulocal_var(ptproc), RTS_SLOT_FREE));
    assert(get_cpulocal_var(ptproc)->pt_handler != 0xFFFFFFFF);
    if (srcproc) assert(!RTS_ISSET(srcproc, RTS_VMINHIBIT));
    if (dstproc) assert(!RTS_ISSET(dstproc, RTS_VMINHIBIT));

    while (bytes > 0) {
        phys_bytes srcptr, dstptr;
        vir_bytes  chunk   = bytes;
        int        changed = 0;

        srcptr = createpde(srcproc, srclinaddr, &chunk, 0);
        dstptr = createpde(dstproc, dstlinaddr, &chunk, 1);

        if (srcptr + chunk < srcptr) return EFAULT_SRC;
        if (dstptr + chunk < dstptr) return EFAULT_DST;

        PHYS_COPY_CATCH(srcptr, dstptr, chunk, addr);

        if (addr) {
            vir_bytes src_aligned = srcptr & ~0x3, dst_aligned = dstptr & ~0x3;
            if (addr >= src_aligned && addr < (srcptr + chunk)) return EFAULT_SRC;
            if (addr >= dst_aligned && addr < (dstptr + chunk)) return EFAULT_DST;
            panic("lin_lin_copy fault out of range");
            return EFAULT;
        }

        bytes      -= chunk;
        srclinaddr += chunk;
        dstlinaddr += chunk;
    }

    assert(get_cpulocal_var(ptproc)->pt_handler != 0xFFFFFFFF);
    return OK;
}

/*===========================================================================*
 *                          phys_get32                                       *
 *===========================================================================*/
static u32_t phys_get32(phys_bytes addr)
{
    u32_t v;
    if (lin_lin_copy(NULL, addr,
                     proc_addr(SYSTEM), (phys_bytes)&v, sizeof(v)) != OK)
        panic("lin_lin_copy for phys_get32 failed");
    return v;
}

/*
 * Поиск физического адреса по виртуальному в конкретном пространстве процесса
 * Ищем только по APT
 */
int vm_lookup(const struct proc *proc, const vir_bytes virtual,
              phys_bytes *physical, u32_t *ptent)
{
    assert(proc);
    assert(physical);
    assert(!isemptyp(proc));
    assert(HASPT(proc));

    vm_abstract_pt_t        *apt_table = proc->apt_table;
    vm_abstract_pt_entry_t *entry;
    if (apt_find_entry_by_virt_addr(apt_table, virtual, entry) == OK) {
        if (entry->paddr == 0) {
            *physical = 0;
            return OK;
        }
        *physical = entry->paddr + (virtual - entry->vaddr);
        return OK;
    }

    return EFAULT;
}

/*===========================================================================*
 *                          umap_virtual                                     *
 *===========================================================================*/
phys_bytes umap_virtual(register struct proc *rp, int seg,
                        vir_bytes vir_addr, vir_bytes bytes)
{
    phys_bytes phys = 0;

    if (vm_lookup(rp, vir_addr, &phys, NULL) != OK) {
        printf("SYSTEM:umap_virtual: vm_lookup of %s: seg 0x%x: 0x%lx failed\n",
               rp->p_name, seg, vir_addr);
        return 0;
    }
    if (phys == 0)
        panic("vm_lookup returned phys == 0");

    if (bytes > 0 && vm_lookup_range(rp, vir_addr, NULL, bytes) != bytes) {
        printf("umap_virtual: %s: %lu at 0x%lx not contiguous\n",
               rp->p_name, bytes, vir_addr);
        return 0;
    }

    return phys;
}

/*===========================================================================*
 *                          vm_lookup_range                                  *
 *===========================================================================*/
size_t vm_lookup_range(const struct proc *proc, vir_bytes vir_addr,
                       phys_bytes *phys_addr, size_t bytes)
{
    phys_bytes phys, next_phys;
    size_t len;

    assert(proc && bytes > 0 && HASPT(proc));

    if (vm_lookup(proc, vir_addr, &phys, NULL) != OK)
        return 0;

    if (phys_addr) *phys_addr = phys;

    len       = ARM_PAGE_SIZE - (vir_addr % ARM_PAGE_SIZE);
    vir_addr += len;
    next_phys = phys + len;

    while (len < bytes) {
        if (vm_lookup(proc, vir_addr, &phys, NULL) != OK) break;
        if (next_phys != phys) break;
        len       += ARM_PAGE_SIZE;
        vir_addr  += ARM_PAGE_SIZE;
        next_phys += ARM_PAGE_SIZE;
    }

    return MIN(bytes, len);
}

/*===========================================================================*
 *                          vm_check_range                                   *
 *===========================================================================*/
int vm_check_range(struct proc *caller, struct proc *target,
                   vir_bytes vir_addr, size_t bytes, int writeflag)
{
    int r;
    if ((caller->p_misc_flags & MF_KCALL_RESUME) &&
        (r = caller->p_vmrequest.vmresult) != OK)
        return r;
    vm_suspend(caller, target, vir_addr, bytes, VMSTYPE_KERNELCALL, writeflag);
    return VMSUSPEND;
}

/*===========================================================================*
 *                          vm_memset                                        *
 *===========================================================================*/
int vm_memset(struct proc *caller, endpoint_t who, phys_bytes ph, int c,
              phys_bytes count)
{
    u32_t pattern;
    struct proc *whoptr = NULL;
    phys_bytes cur_ph = ph, left = count;
    phys_bytes ptr, chunk, pfa = 0;
    int new_ttbr, r = OK;

    if ((r = check_resumed_caller(caller)) != OK) return r;

    if (who != NONE && !(whoptr = endpoint_lookup(who))) return ESRCH;

    c &= 0xFF;
    pattern = c | (c << 8) | (c << 16) | (c << 24);

    assert(get_cpulocal_var(ptproc)->pt_handler != 0xFFFFFFFF);
    assert(!catch_pagefaults);
    catch_pagefaults = 1;

    while (left > 0) {
        chunk    = left;
        ptr      = createpde(whoptr, cur_ph, &chunk, 0);

        if ((pfa = phys_memset(ptr, pattern, chunk))) {
            if (whoptr) {
                vm_suspend(caller, whoptr, ph, count, VMSTYPE_KERNELCALL, 1);
                assert(catch_pagefaults);
                catch_pagefaults = 0;
                return VMSUSPEND;
            }
            panic("vm_memset: pf %lx addr=%lx len=%lu", pfa, ptr, chunk);
        }
        cur_ph += chunk;
        left   -= chunk;
    }

    assert(get_cpulocal_var(ptproc)->pt_handler != 0xFFFFFFF);
    assert(catch_pagefaults);
    catch_pagefaults = 0;
    return OK;
}

/*===========================================================================*
 *                          virtual_copy_f                                   *
 *===========================================================================*/
int virtual_copy_f(struct proc *caller, struct vir_addr *src_addr,
                   struct vir_addr *dst_addr, vir_bytes bytes, int vmcheck)
{
    struct vir_addr *vir_addr[2];
    int i, r;
    struct proc *procs[2];

    assert((vmcheck && caller) || (!vmcheck && !caller));
    if (bytes <= 0) return EDOM;

    vir_addr[_SRC_] = src_addr;
    vir_addr[_DST_] = dst_addr;

    for (i = _SRC_; i <= _DST_; i++) {
        endpoint_t proc_e = vir_addr[i]->proc_nr_e;
        int proc_nr;
        if (proc_e == NONE) {
            procs[i] = NULL;
        } else {
            if (!isokendpt(proc_e, &proc_nr)) {
                printf("virtual_copy: no reasonable endpoint\n");
                return ESRCH;
            }
            procs[i] = proc_addr(proc_nr);
        }
    }

    if ((r = check_resumed_caller(caller)) != OK) return r;

    if ((r = lin_lin_copy(procs[_SRC_], vir_addr[_SRC_]->offset,
                          procs[_DST_], vir_addr[_DST_]->offset,
                          bytes)) != OK) {
        struct proc *target = NULL;
        phys_bytes lin;
        int writeflag;
        if (r != EFAULT_SRC && r != EFAULT_DST)
            panic("lin_lin_copy failed: %d", r);
        if (!vmcheck || !caller) return r;
        if (r == EFAULT_SRC) {
            lin = vir_addr[_SRC_]->offset; target = procs[_SRC_]; writeflag = 0;
        } else {
            lin = vir_addr[_DST_]->offset; target = procs[_DST_]; writeflag = 1;
        }
        assert(caller && target);
        vm_suspend(caller, target, lin, bytes, VMSTYPE_KERNELCALL, writeflag);
        return VMSUSPEND;
    }

    return OK;
}

/*===========================================================================*
 *                          data_copy / data_copy_vmcheck                   *
 *===========================================================================*/
int data_copy(const endpoint_t from_proc, const vir_bytes from_addr,
              const endpoint_t to_proc,   const vir_bytes to_addr,
              size_t bytes)
{
    struct vir_addr src, dst;
    src.offset = from_addr; src.proc_nr_e = from_proc;
    dst.offset = to_addr;   dst.proc_nr_e = to_proc;
    assert(src.proc_nr_e != NONE && dst.proc_nr_e != NONE);
    return virtual_copy(&src, &dst, bytes);
}

int data_copy_vmcheck(struct proc *caller,
                      const endpoint_t from_proc, const vir_bytes from_addr,
                      const endpoint_t to_proc,   const vir_bytes to_addr,
                      size_t bytes)
{
    struct vir_addr src, dst;
    src.offset = from_addr; src.proc_nr_e = from_proc;
    dst.offset = to_addr;   dst.proc_nr_e = to_proc;
    assert(src.proc_nr_e != NONE && dst.proc_nr_e != NONE);
    return virtual_copy_vmcheck(caller, &src, &dst, bytes);
}

/*===========================================================================*
 *                          arch_proc_init                                   *
 *===========================================================================*/
void arch_proc_init(struct proc *pr, const u32_t ip, const u32_t sp,
                    const u32_t ps_str, char *name)
{
    arch_proc_reset(pr);
    strcpy(pr->p_name, name);
    pr->p_reg.pc      = ip;
    pr->p_reg.sp      = sp;
    pr->p_reg.retreg  = ps_str;  /* r0 */
    pr->pt_handler    = PT_HANDLER_NONE; /* будет выставлен в arch_boot_proc */
}

int arch_enable_paging(struct proc *caller)
{
    kern_phys_map *phys_maps;
    assert(caller->p_seg.p_ttbr);
    switch_address_space(caller);
    phys_maps = kern_phys_map_head;
    while (phys_maps) {
        assert(phys_maps->cb != NULL);
        phys_maps->cb(phys_maps->id, phys_maps->vir);
        phys_maps = phys_maps->next;
    }
    return OK;
}

void release_address_space(struct proc *pr)
{
    pr->p_seg.p_ttbr_v = NULL;
    barrier();
}
