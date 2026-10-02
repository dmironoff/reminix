/*
 * abi64 probe: IPC messages, boot loader and partition table structures,
 * kernel/process boundary types.  Compiled against the MINIX headers for
 * -m32 or -m64 (mkinc.sh); every entry goes to the "abi64_tab" section
 * (abi64.h).  File systems and devices: probe_fs.c, probe_dev.c.
 */
#define ABI64_PROBE
#define ABI64_MSGS
#include "abi64.h"

/* Messages: every _ASSERT_MSG_SIZE() in ipc.h records its type. */
#include <minix/ipc.h>

#include <minix/type.h>
#include <minix/param.h>
#include <minix/safecopies.h>
#include <minix/vm.h>
#include <minix/rs.h>
#include <minix/partition.h>
#include <minix/ipc_filter.h>
#include <machine/multiboot.h>
#include <machine/partition.h>
#include <machine/archtypes.h>
#include <machine/stackframe.h>
#include <machine/mcontext.h>
#include <machine/signal.h>
#include <sys/exec.h>

/* ext: boot loader (Multiboot 0.6.96, packed structures). */
ABI64_TYPE("ext", struct multiboot_info);
ABI64_FIELD("ext", struct multiboot_info, mi_cmdline);
ABI64_FIELD("ext", struct multiboot_info, mi_mods_count);
ABI64_FIELD("ext", struct multiboot_info, mi_mods_addr);
ABI64_FIELD("ext", struct multiboot_info, mi_elfshdr_addr);
ABI64_FIELD("ext", struct multiboot_info, mi_mmap_length);
ABI64_FIELD("ext", struct multiboot_info, mi_mmap_addr);
ABI64_FIELD("ext", struct multiboot_info, mi_drives_addr);
ABI64_FIELD("ext", struct multiboot_info, mi_loader_name);
ABI64_FIELD("ext", struct multiboot_info, vbe_control_info);
ABI64_FIELD("ext", struct multiboot_info, framebuffer_addr);
ABI64_FIELD("ext", struct multiboot_info, framebuffer_pitch);
ABI64_FIELD("ext", struct multiboot_info, framebuffer_type);
ABI64_TYPE("ext", struct multiboot_mmap);
ABI64_FIELD("ext", struct multiboot_mmap, mm_base_addr);
ABI64_FIELD("ext", struct multiboot_mmap, mm_length);
ABI64_FIELD("ext", struct multiboot_mmap, mm_type);
ABI64_TYPE("ext", struct multiboot_module);
ABI64_FIELD("ext", struct multiboot_module, mmo_string);
ABI64_FIELD("ext", struct multiboot_module, mmo_reserved);
ABI64_TYPE("ext", struct multiboot_drive);

/* ext: disk. */
ABI64_TYPE("ext", struct part_entry);
ABI64_FIELD("ext", struct part_entry, lowsec);
ABI64_FIELD("ext", struct part_entry, size);

/* bnd: shared between the kernel, servers and processes. */
ABI64_TYPE("bnd", kinfo_t);
ABI64_TYPE("bnd", struct machine);
ABI64_TYPE("bnd", struct minix_kerninfo);
ABI64_TYPE("bnd", struct kmessages);
ABI64_TYPE("bnd", struct kclockinfo);
ABI64_TYPE("bnd", struct kuserinfo);
ABI64_TYPE("bnd", struct loadinfo);
ABI64_TYPE("bnd", struct k_randomness);
ABI64_TYPE("bnd", struct boot_image);
ABI64_TYPE("bnd", struct memory);
ABI64_TYPE("bnd", struct sigmsg);
ABI64_TYPE("bnd", struct vir_addr);
ABI64_TYPE("bnd", struct vumap_vir);
ABI64_TYPE("bnd", struct vumap_phys);
ABI64_TYPE("bnd", cp_grant_t);
ABI64_TYPE("bnd", struct vscp_vec);
ABI64_TYPE("bnd", struct vm_stats_info);
ABI64_TYPE("bnd", struct vm_usage_info);
ABI64_TYPE("bnd", struct vm_region_info);
ABI64_TYPE("bnd", struct rprocpub);
ABI64_TYPE("bnd", struct rs_state_data);
ABI64_TYPE("bnd", struct part_geom);
ABI64_TYPE("bnd", ipc_filter_el_t);
ABI64_TYPE("bnd", struct stackframe_s);
ABI64_TYPE("bnd", struct segframe);
ABI64_TYPE("bnd", mcontext_t);
ABI64_TYPE("bnd", struct sigcontext);
ABI64_TYPE("bnd", struct ps_strings);
