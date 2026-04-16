/* protect.c — ReMinix ARM32 protection / boot proc setup.
 * arch_boot_proc() uses BKI->modules[] instead of multiboot module_list.
 */

#include <assert.h>
#include <string.h>

#include "kernel/kernel.h"
#include "archconst.h"
#include "arch_proto.h"
#include "kernel/bootstrap_kernel_information.h"
#include "kernel/mmap_utils.h"
#include "kernel/apt_utils.h"
#include <sys/exec.h>
#include <libexec.h>

/* Defined in main.c */
extern bootstrap_kernel_information_t *BKI;

struct tss_s tss[CONFIG_MAX_CPUS];
extern int exc_vector_table;

int prot_init_done = 0;

/* ------------------------------------------------------------------ */

phys_bytes vir2phys(void *vir)
{
    extern char _kern_vir_base, _kern_phys_base;
    u32_t offset = (vir_bytes)&_kern_vir_base - (vir_bytes)&_kern_phys_base;
    return (phys_bytes)vir - offset;
}

int tss_init(unsigned cpu, void *kernel_stack)
{
    struct tss_s *t = &tss[cpu];
    t->sp0 = ((unsigned)kernel_stack) - ARM_STACK_TOP_RESERVED;
    *((reg_t *)(t->sp0 + 1 * sizeof(reg_t))) = cpu;
    return 0;
}

int booting_cpu = 0;

void prot_init(void)
{
    write_vbar((reg_t)&exc_vector_table);
    prot_init_done = 1;
}

void arch_post_init(void)
{
    /* После перехода к работающему MMU ptproc указывает на VM */
    struct proc *vm = proc_addr(VM_PROC_NR);
    get_cpulocal_var(ptproc) = vm;
}

/* ------------------------------------------------------------------ */

/*
 * Вспомогательная: найти boot_module_information_t по имени в BKI->modules[].
 * Возвращает NULL если не нашли.
 */
static boot_module_information_t *bki_find_module(const char *name)
{
    int i;
    for (i = 0; i < BOOT_MODULES_MAX_COUNT; i++) {
        if (BKI->modules[i].type == BOOT_MODULE_UNKNOWN)
            continue;
        if (strcmp(BKI->modules[i].name, name) == 0)
            return &BKI->modules[i];
    }
    return NULL;
}

/* ------------------------------------------------------------------ */

static int alloc_for_vm = 0;

/*
 * libexec callback: выделить страницу под VM в bootstrap APT.
 * На этом этапе VM ещё грузится в то же адресное пространство что и ядро
 * (apt_vm_process_prototype уже подготовлен в pre_init).
 */
static int libexec_pg_alloc(struct exec_info *execi, vir_bytes vaddr, size_t len)
{
    /* Используем BKI->apt_vm_process_prototype для разметки виртуальной
     * памяти VM «по требованию» — ядро потом подберёт физику при pagefault.
     * Пока всё ещё flat: просто помечаем диапазон как PRESENT + RWX в apt. */
    int res;
    res = apt_map_phys_to_vir(BKI->apt,
                              BKI->apt_vm_process_prototype,
                              (phys_bytes) vaddr,   /* 1:1 на этом этапе */
                              (vir_bytes)  len,
                              vaddr,
                              VM_APF_PRESENT | VM_APF_RWX | VM_APF_USER,
                              MMAP_CACHE_NORMAL);
    if (res < 0)
        panic("libexec_pg_alloc: apt_map_phys_to_vir failed: %d", res);

    memset((void *)vaddr, 0, len);
    alloc_for_vm += len;
    return OK;
}

/*===========================================================================*
 *                          arch_boot_proc                                   *
 *===========================================================================*/
void arch_boot_proc(struct boot_image *ip, struct proc *rp)
{
    boot_module_information_t *mod;
    struct ps_strings *psp;
    char *sp;

    if (rp->p_nr < 0)
        return;

    /* Найдём модуль в BKI по имени процесса */
    mod = bki_find_module(ip->proc_name);

    if (rp->p_nr == VM_PROC_NR) {
        struct exec_info execi;

        if (!mod)
            panic("arch_boot_proc: VM module not found in BKI");

        memset(&execi, 0, sizeof(execi));

        execi.stack_high = kinfo.user_sp;
        execi.stack_size = 64 * 1024;
        execi.proc_e     = ip->endpoint;
        execi.hdr        = (char *) mod->addr;   /* физический адрес */
        execi.filesize   = execi.hdr_len = mod->size;
        strlcpy(execi.progname, ip->proc_name, sizeof(execi.progname));
        execi.frame_len  = 0;

        execi.copymem               = libexec_copy_memcpy;
        execi.clearmem              = libexec_clear_memset;
        execi.allocmem_prealloc_junk    = libexec_pg_alloc;
        execi.allocmem_prealloc_cleared = libexec_pg_alloc;
        execi.allocmem_ondemand         = libexec_pg_alloc;
        execi.clearproc = NULL;

        if (libexec_load_elf(&execi) != OK)
            panic("VM ELF loading failed");

        sp = (char *)execi.stack_high;
        sp -= sizeof(struct ps_strings);
        psp = (struct ps_strings *)sp;
        sp -= (sizeof(void *) + sizeof(void *) + sizeof(int));

        psp->ps_argvstr  = (char **)(sp + sizeof(int));
        psp->ps_nargvstr = 0;
        psp->ps_envstr   = psp->ps_argvstr + sizeof(void *);
        psp->ps_nenvstr  = 0;

        arch_proc_init(rp, execi.pc, (vir_bytes)sp,
                       execi.stack_high - sizeof(struct ps_strings),
                       ip->proc_name);

        /* Привяжем к процессу его APT-прототип и физическую таблицу.
         * vm_arch_apt_to_pt вызовем здесь для VM, что бы у него был
         * рабочий ttbr0 с момента первого запуска. */
        phys_bytes vm_pt_root;
        uint32_t   vm_pt_handler;
        int res = vm_arch_alloc_pagetable(BKI->arch_pt_base,
                                          BKI->mmap,
                                          BKI->apt,
                                          BKI->kernel_apt,
                                          ip->endpoint,
                                          &vm_pt_root,
                                          &vm_pt_handler);
        if (res != OK)
            panic("arch_boot_proc: vm_arch_alloc_pagetable for VM failed: %d", res);

        /* Синхронизируем APT VM-процесса в физическую таблицу */
        res = vm_arch_apt_to_pt(BKI->apt,
                                BKI->apt_vm_process_prototype,
                                BKI->arch_pt_base,
                                vm_pt_handler);
        if (res != OK)
            panic("arch_boot_proc: vm_arch_apt_to_pt for VM failed: %d", res);

        rp->p_seg.p_ttbr   = vm_pt_root;
        rp->p_seg.p_ttbr_v = ((arm_pt_t *)BKI->arch_pt_base)[vm_pt_handler].l1_table;
        rp->pt_handler     = vm_pt_handler;

        /* Освобождать blob VM не будем — mmap_free_memory дёрнет VM сам
         * когда запустится и получит управление. */
       // kinfo.vm_allocated_bytes = alloc_for_vm;
    }
    /* Все остальные boot-процессы: ядро инициализирует их регистры,
     * таблицы страниц выставит VM после старта через VMCTL. */
}
