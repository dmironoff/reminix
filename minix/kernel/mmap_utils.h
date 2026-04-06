//
// Created by dmironov on 20.03.2026.
//

#ifndef REMINIX_MMAP_UTILS_H
#define REMINIX_MMAP_UTILS_H

#define MMAP_ERROR_NOT_AVALIBLE_REGIONS     -1
#define MMAP_ERROR_INCORRECT_ADDR           -2
#define MMAP_ERROR_INDIVIDUAL_ONLY          -3
#define MMAP_ERROR_REGION_BUSY              -4


#include <minix/physmemorymap.h>

int mmap_find_region_by_addr(mmap_t *mmap, phys_bytes addr, mmap_region_t *region);
int mmap_find_undef_region_record(mmap_t *mmap, mmap_region_t *region);
int mmap_add_region(mmap_t *mmap, mmap_region_t *region);
int mmap_unmap_region(mmap_t *mmap, mmap_region_t *region);
int mmap_find_lowest_free_region(mmap_t *mmap, phys_bytes size, mmap_region_t *region);
int mmap_copy_to_new_location(mmap_t *mmap, mmap_t *new_mmap);
int mmap_find_lowest_free_aligned_region(mmap_t *mmap, phys_bytes size, phys_bytes align, mmap_region_t *region);

#endif //REMINIX_MMAP_UTILS_H
