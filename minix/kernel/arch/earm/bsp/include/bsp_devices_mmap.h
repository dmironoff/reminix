//
// Created by dmironov on 19.03.2026.
//

#ifndef REMINIX_BSP_DEVICES_MMAP_H
#define REMINIX_BSP_DEVICES_MMAP_H

typedef struct {
    phys_bytes        start;
    phys_bytes        size;
} bsp_devices_mmap_t;

void bsp_devices_mmap (bsp_devices_mmap_t *mmap, uint32_t   *count);

#endif //REMINIX_BSP_DEVICES_MMAP_H
