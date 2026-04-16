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

extern bootstrap_kernel_information_t *BKI;
extern vm_abstract_pagetables_t       *system_apt;
extern vir_bytes                       user_pt_base; /* arch_pt_base из BKI */

/* ------------------------------------------------------------------ */
/* Совместимость: HASPT проверяет p_ttbr как и раньше                 */
#define HASPT(procptr) ((procptr)->p_seg.p_ttbr != 0)

static int nfreepdes = 0;
#define MAXFREEPDES 2
static int freepdes[MAXFREEPDES];   /* индексы L1 в текущей таблице ядра */

static u32_t phys_get32(phys_bytes v);

/* list of requested physical mapping */
static kern_phys_map *kern_phys_map_head;

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
    freepdes[nfreepdes++] = ARM_L1_INDEX(BKI->vir_memory_cp_region_addr);
    freepdes[nfreepdes++] = ARM_L1_INDEX(BKI->vir_memory_cp_region_addr
                                         + ARM_L1_SIZE);

    assert(nfreepdes == MAXFREEPDES);
}

/*===========================================================================*
 *                          mem_clear_mapcache                               *
 *===========================================================================*/
void mem_clear_mapcache(void)
{
    int i;
    for (i = 0; i < nfreepdes; i++) {
        struct proc *ptproc = get_cpulocal_var(ptproc);
        int pde = freepdes[i];
        u32_t *ptv;
        assert(ptproc);
        ptv = ptproc->p_seg.p_ttbr_v;
        assert(ptv);
        ptv[pde] = 0;
    }
}

/*===========================================================================*
 *                          createpde                                        *
 *===========================================================================*/
static phys_bytes createpde(
        const struct proc *pr,
        const phys_bytes   linaddr,
        phys_bytes        *bytes,
        int                free_pde_idx,
        int               *changed)
{
    u32_t pdeval;
    phys_bytes offset;
    int pde;

    assert(free_pde_idx >= 0 && free_pde_idx < nfreepdes);
    pde = freepdes[free_pde_idx];
    assert(pde >= 0 && pde < 4096);

    if (pr && ((pr == get_cpulocal_var(ptproc)) || iskernelp(pr)))
        return linaddr;

    if (pr) {
        assert(pr->p_seg.p_ttbr_v);
        pdeval = pr->p_seg.p_ttbr_v[ARM_VM_PDE(linaddr)];
    } else {
        pdeval = (linaddr & ARM_VM_SECTION_MASK)
                 | ARM_VM_SECTION
                 | ARM_VM_SECTION_DOMAIN
                 | ARM_VM_SECTION_CACHED
                 | ARM_VM_SECTION_USER;
    }

    assert(get_cpulocal_var(ptproc)->p_seg.p_ttbr_v);
    if (get_cpulocal_var(ptproc)->p_seg.p_ttbr_v[pde] != pdeval) {
        get_cpulocal_var(ptproc)->p_seg.p_ttbr_v[pde] = pdeval;
        *changed = 1;
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
    assert(get_cpulocal_var(ptproc)->p_seg.p_ttbr_v);
    if (srcproc) assert(!RTS_ISSET(srcproc, RTS_VMINHIBIT));
    if (dstproc) assert(!RTS_ISSET(dstproc, RTS_VMINHIBIT));

    while (bytes > 0) {
        phys_bytes srcptr, dstptr;
        vir_bytes  chunk   = bytes;
        int        changed = 0;

        unsigned cpu = cpunr;
        if (srcproc && GET_BIT(srcproc->p_stale_tlb, cpu)) {
            changed = 1; UNSET_BIT(srcproc->p_stale_tlb, cpu);
        }
        if (dstproc && GET_BIT(dstproc->p_stale_tlb, cpu)) {
            changed = 1; UNSET_BIT(dstproc->p_stale_tlb, cpu);
        }
        srcptr = createpde(srcproc, srclinaddr, &chunk, 0, &changed);
        dstptr = createpde(dstproc, dstlinaddr, &chunk, 1, &changed);
        if (changed)
            reload_ttbr0();

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

    assert(get_cpulocal_var(ptproc)->p_seg.p_ttbr_v);
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

/*===========================================================================*
 *                          vm_lookup                                        *
 *
 * Сначала ищем в APT (быстро, не надо ходить в физическую память).
 * Если APT не знает — откатываемся к физической таблице через phys_get32.
 *===========================================================================*/
int vm_lookup(const struct proc *proc, const vir_bytes virtual,
              phys_bytes *physical, u32_t *ptent)
{
    u32_t *root, *pt;
    int pde, pte;
    u32_t pde_v, pte_v;

    assert(proc);
    assert(physical);
    assert(!isemptyp(proc));
    assert(HASPT(proc));

    /* --- Попытка через APT --- */
    if (system_apt) {
        vm_abstract_pt_t        *apt_table = NULL;
        vm_abstract_pt_l1_entry_t *l1e     = NULL;
        vm_abstract_pt_l2_entry_t *l2e     = NULL;

        /* Ищем таблицу процесса */
        if (apt_find_table_by_endpoint(system_apt,
                                       proc->p_endpoint,
                                       apt_table) == OK && apt_table) {
            if (apt_find_l1_entry_by_virt_addr(apt_table,
                                               virtual, l1e) == OK && l1e) {
                if (l1e->type == VM_APT_L1_SECTION) {
                    /* Секция L1: физический адрес = paddr + offset в секции */
                    if (l1e->flags & VM_APF_VIRTUAL_ONLY) return EFAULT;
                    *physical = l1e->paddr + (virtual - l1e->vaddr);
                    if (ptent) *ptent = 0; /* нет PTE у section */
                    return OK;
                } else {
                    /* L2 таблица */
                    if (apt_find_l2_entry_by_virt_addr(l1e,
                                                       virtual, l2e) == OK
                        && l2e) {
                        if (l2e->flags & VM_APF_VIRTUAL_ONLY) return EFAULT;
                        *physical = l2e->paddr + (virtual - l2e->vaddr);
                        if (ptent) *ptent = 0;
                        return OK;
                    }
                }
            }
        }
        /* APT не знает — продолжаем через физическую таблицу */
    }

    /* --- Физическая таблица (как в оригинале) --- */
    root = (u32_t *)(proc->p_seg.p_ttbr & ARM_TTBR_ADDR_MASK);
    assert(!((u32_t)root % ARM_PAGEDIR_SIZE));
    pde   = ARM_VM_PDE(virtual);
    assert(pde >= 0 && pde < ARM_VM_DIR_ENTRIES);
    pde_v = phys_get32((u32_t)(root + pde));

    if (!((pde_v & ARM_VM_PDE_PRESENT) || (pde_v & ARM_VM_SECTION_PRESENT)))
        return EFAULT;

    if (pde_v & ARM_VM_SECTION) {
        *physical = pde_v & ARM_VM_SECTION_MASK;
        if (ptent) *ptent = pde_v;
        *physical += virtual & ARM_VM_OFFSET_MASK_1MB;
    } else {
        pt  = (u32_t *)(pde_v & ARM_VM_PDE_MASK);
        assert(!((u32_t)pt % ARM_PAGETABLE_SIZE));
        pte   = ARM_VM_PTE(virtual);
        assert(pte >= 0 && pte < ARM_VM_PT_ENTRIES);
        pte_v = phys_get32((u32_t)(pt + pte));
        if (!(pte_v & ARM_VM_PTE_PRESENT))
            return EFAULT;
        if (ptent) *ptent = pte_v;
        *physical  = pte_v & ARM_VM_PTE_MASK;
        *physical += virtual % ARM_PAGE_SIZE;
    }

    return OK;
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

    assert(get_cpulocal_var(ptproc)->p_seg.p_ttbr_v);
    assert(!catch_pagefaults);
    catch_pagefaults = 1;

    while (left > 0) {
        new_ttbr = 0;
        chunk    = left;
        ptr      = createpde(whoptr, cur_ph, &chunk, 0, &new_ttbr);
        if (new_ttbr) reload_ttbr0();

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

    assert(get_cpulocal_var(ptproc)->p_seg.p_ttbr_v);
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

/*===========================================================================*
 *                      switch_address_space                                 *
 *
 * Используется при переключении контекста (вызывается из mpx.S через
 * switch_address_space(proc)).  Если у процесса есть pt_handler — грузим
 * через pg_load_ttbr0(); иначе (ядровые задачи) — ничего не делаем.
 *===========================================================================*/
/*
void switch_address_space(struct proc *p)
{
    arm_pt_t *pagetables = (arm_pt_t *) user_pt_base;

    if (p->pt_handler != PT_HANDLER_NONE &&
        p->pt_handler < ARM_MAX_PT_HANDLES) {
        pg_load_ttbr0(&pagetables[p->pt_handler]);
    }

    get_cpulocal_var(ptproc) = p;
}
*/

/* ------------------------------------------------------------------ */
/* Оставшиеся функции arch_phys_map / arch_phys_map_reply /           */
/* arch_enable_paging / kern_phys_map_ptr — без изменений             */
/* ------------------------------------------------------------------ */

static int usermapped_glo_index = -1,
        usermapped_index = -1, first_um_idx = -1;

extern char usermapped_start, usermapped_end, usermapped_nonglo_start;

int arch_phys_map(const int index, phys_bytes *addr,
                  phys_bytes *len, int *flags)
{
    static int first = 1;
    kern_phys_map *phys_maps;
    int freeidx = 0;
    u32_t glo_len = (u32_t)&usermapped_nonglo_start - (u32_t)&usermapped_start;

    if (first) {
        memset(&minix_kerninfo, 0, sizeof(minix_kerninfo));
        if (glo_len > 0)      usermapped_glo_index = freeidx++;
        usermapped_index = freeidx++;
        first_um_idx     = (usermapped_glo_index != -1) ?
                           usermapped_glo_index : usermapped_index;
        first = 0;
        phys_maps = kern_phys_map_head;
        while (phys_maps) { phys_maps->index = freeidx++; phys_maps = phys_maps->next; }
    }

    if (index == usermapped_glo_index) {
        *addr = vir2phys(&usermapped_start); *len = glo_len;
        *flags = VMMF_USER | VMMF_GLO; return OK;
    }
    if (index == usermapped_index) {
        *addr  = vir2phys(&usermapped_nonglo_start);
        *len   = (u32_t)&usermapped_end - (u32_t)&usermapped_nonglo_start;
        *flags = VMMF_USER; return OK;
    }

    phys_maps = kern_phys_map_head;
    while (phys_maps) {
        if (phys_maps->index == index) {
            *addr = phys_maps->addr; *len = phys_maps->size;
            *flags = phys_maps->vm_flags; return OK;
        }
        phys_maps = phys_maps->next;
    }
    return EINVAL;
}

int arch_phys_map_reply(const int index, const vir_bytes addr)
{
    kern_phys_map *phys_maps;

    if (index == first_um_idx) {
        u32_t usermapped_offset = addr - (u32_t)&usermapped_start;
#define FIXEDPTR(ptr) (void *)((u32_t)(ptr) + usermapped_offset)
#define FIXPTR(ptr)   ptr = FIXEDPTR(ptr)
#define ASSIGN(s)     minix_kerninfo.s = FIXEDPTR(&s)
        ASSIGN(kinfo); ASSIGN(machine); ASSIGN(kmessages); ASSIGN(loadinfo);
        ASSIGN(kuserinfo); ASSIGN(arm_frclock); ASSIGN(kclockinfo);
        minix_kerninfo.kerninfo_magic      = KERNINFO_MAGIC;
        minix_kerninfo.minix_feature_flags = minix_feature_flags;
        minix_kerninfo_user = (vir_bytes)FIXEDPTR(&minix_kerninfo);
        minix_kerninfo.ki_flags |= MINIX_KIF_USERINFO;
        return OK;
    }
    if (index == usermapped_index) return OK;

    phys_maps = kern_phys_map_head;
    while (phys_maps) {
        if (phys_maps->index == index) {
            assert(phys_maps->cb != NULL);
            phys_maps->vir = addr;
            return OK;
        }
        phys_maps = phys_maps->next;
    }
    return EINVAL;
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

int kern_req_phys_map(phys_bytes base_address, vir_bytes io_size,
                      int vm_flags, kern_phys_map *priv,
                      kern_phys_map_mapped cb, vir_bytes id)
{
    assert(base_address != 0);
    assert(io_size % ARM_PAGE_SIZE == 0);
    assert(cb != NULL);
    priv->addr = base_address; priv->size = io_size;
    priv->vm_flags = vm_flags; priv->cb = cb;
    priv->id = id; priv->index = -1; priv->next = NULL;
    if (kern_phys_map_head == NULL) {
        kern_phys_map_head = priv; kern_phys_map_head->next = NULL;
    } else {
        priv->next = kern_phys_map_head; kern_phys_map_head = priv;
    }
    return 0;
}

int kern_phys_map_mapped_ptr(vir_bytes id, phys_bytes address)
{
    *((vir_bytes *)id) = address;
    return 0;
}

int kern_phys_map_ptr(phys_bytes base_address, vir_bytes io_size,
                      int vm_flags, kern_phys_map *priv, vir_bytes ptr)
{
    return kern_req_phys_map(base_address, io_size, vm_flags, priv,
                             kern_phys_map_mapped_ptr, ptr);
}
