//
// Created by dmironov on 09.03.2026.
//

#ifndef REMINIX_MODULES_MEMORY_MAP_H
#define REMINIX_MODULES_MEMORY_MAP_H

/* XXX: hard-coded stuff for modules */
#define MB_MODS_NR NR_BOOT_MODULES
#define MB_MODS_BASE  0x82000000
#define MB_MODS_ALIGN 0x00800000 /* 8 MB */
#define MB_MMAP_START 0x80000000
#define MB_MMAP_SIZE  0x10000000 /* 256 MB */

#endif //REMINIX_MODULES_MEMORY_MAP_H
