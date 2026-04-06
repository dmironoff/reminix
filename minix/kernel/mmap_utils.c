//
// Created by dmironov on 19.03.2026.
//

#include <sys/types.h>
#include <minix/endpoint.h>
#include "string.h"
#include <minix/physmemorymap.h>
#include "mmap_utils.h"

/*
 * Поиск региона памяти по адресу памяти
 * Возвращает указатель на регион памяти в карте через переменную region
 */
int mmap_find_region_by_addr(mmap_t *mmap, phys_bytes addr, mmap_region_t *region) {
    mmap_region_t *iter;
    for (iter = mmap->first_region; iter->next_region != 0; iter = (mmap_region_t *)iter->next_region) {
        if (iter->start <= addr && (iter->start + iter->size) > addr) {
            region = iter;
            return 1;
        }
    }
    // Последний элемент списка
    // Пора читать Кнута
    if (iter->start <= addr && (iter->start + iter->size) > addr) {
        region = iter;
        return 1;

    }
    return MMAP_ERROR_INCORRECT_ADDR;
}

/*
 * Поиск следующей нераспределённой записи
 * в буффере регионов памяти
 */
int mmap_find_undef_region_record(mmap_t *mmap, mmap_region_t *region) {
    if (mmap->regions_count == mmap->regions_allocated) {
        return MMAP_ERROR_NOT_AVALIBLE_REGIONS;
    }
    for (uint32_t i = 0; i < mmap->regions_allocated; i++) {
        if (mmap->regions[i].type == MMAP_UNDEF) {
            region = &mmap->regions[i];
            return 1;
        }
    }
    return MMAP_ERROR_NOT_AVALIBLE_REGIONS;
}

/*
 * Добавление региона памяти в карту
 */
int mmap_add_region (mmap_t *mmap, mmap_region_t *region)  {
    mmap_region_t *free_region_record;
    mmap_region_t *free_region_record_2;
    mmap_region_t *working_region;


    region->start = (region->start+1024*1024) & ~(1024*1024); // НУ НАХУЙ ВСЁ ВЫРАВНИВАЕМ ПО 1 МБ
    region->size = (region->size+1024+1024) & ~(1024*1024);

    mmap_find_region_by_addr(mmap, region->start, working_region);

    if (working_region == 0) {
        return MMAP_ERROR_INCORRECT_ADDR;
    }

    if (working_region->flags & MMAP_REGION_DEVICE_INDIVIDUAL && working_region->refcount > 0) {
        return MMAP_ERROR_INDIVIDUAL_ONLY;
    }

    if (working_region->start == region->start && working_region->size == region->size) {
        //  Нам повезло - у нас уже есть этот прекрасный кусок
        if (working_region->type == MMAP_UNDEF || working_region->type == MMAP_FREE) {
            working_region->type = region->type;
            working_region->flags = region->flags;
            working_region->cache_hint = region->cache_hint;
            memcpy(working_region, region, sizeof(mmap_region_t));
            mmap->free_mem -= region->size;
            mmap->version++;
            return 1;
        } else if (working_region->type == region->type) {
            memcpy(working_region, region, sizeof(mmap_region_t));
            return 1;
        } else {
            return MMAP_ERROR_REGION_BUSY;
        }
    }

    if (mmap->regions_count == mmap->regions_allocated) {
        return MMAP_ERROR_NOT_AVALIBLE_REGIONS;
    }

    if (mmap_find_undef_region_record(mmap, free_region_record) < 0) {
        return MMAP_ERROR_NOT_AVALIBLE_REGIONS;
    }

    if ((working_region->type == region->type && (region->type == MMAP_DEVICE || region->type == MMAP_DMA)) ||
                working_region->type == MMAP_UNDEF ||
                working_region->type == MMAP_FREE) {
        if (working_region->start + working_region->size == region->start + region->size) {
            // Новый регион вконце существующего
            free_region_record->prev_region = working_region;
            if (working_region->next_region != 0) {
                free_region_record->next_region = working_region->next_region;
                ( (mmap_region_t *)working_region->next_region)->prev_region = free_region_record;
            }
            working_region->next_region = free_region_record;
            free_region_record->type = region->type;
            free_region_record->cache_hint = region->cache_hint;
            free_region_record->flags = region->flags;
            free_region_record->start = region->start;
            free_region_record->size = region->size;
            working_region->size -= region->size;
            if (mmap->last_region == working_region) {
                mmap->last_region = free_region_record;
            }
            mmap->regions_count++;
        } else if (working_region->start == region->start) {
            // В начале
            if (working_region->prev_region != 0) {
                ( (mmap_region_t *)working_region->prev_region)->next_region = free_region_record;
                free_region_record->prev_region = working_region->prev_region;
            }
            working_region->prev_region = free_region_record;
            free_region_record->next_region = working_region;
            free_region_record->type = region->type;
            free_region_record->cache_hint = region->cache_hint;
            free_region_record->flags = region->flags;
            free_region_record->start = region->start;
            free_region_record->size = region->size;
            working_region->size -= region->size;
            working_region->start += region->size;
            if (mmap->first_region == working_region) {
                mmap->first_region = free_region_record;
            }
            mmap->regions_count++;
        } else {
            // Посередине
            free_region_record->type = region->type;
            mmap_find_undef_region_record(mmap, free_region_record_2);

            free_region_record_2->next_region = working_region->next_region;
            free_region_record_2->prev_region = free_region_record;
            working_region->next_region = free_region_record;
            free_region_record->prev_region = working_region;

            free_region_record_2->type = working_region->type;
            free_region_record_2->flags = working_region->flags;
            free_region_record_2->cache_hint = working_region->cache_hint;
            working_region->size = region->start - working_region->start;
            free_region_record_2->start = region->start + region->size;
            free_region_record_2->size = ((mmap_region_t *)free_region_record_2->next_region)->start - free_region_record_2->start;

            free_region_record->type = region->type;
            free_region_record->start = region->start;
            free_region_record->size = region->size;
            free_region_record->flags = region->flags;
            free_region_record->cache_hint = region->cache_hint;

            region = free_region_record;
            mmap->regions_count += 2;
        }
        if (working_region->type == MMAP_FREE || working_region->type == MMAP_UNDEF) {
            mmap->free_mem -= free_region_record->size;
        }
        mmap->version++;
        return 1;
    }


    return 0;
}


/*
 * Освобождение региона памяти
 * ВНИМАНИЕ! Это низкоуровневая функция и она не занимается подсчётом refcounts
 */
int mmap_unmap_region(mmap_t *mmap, mmap_region_t *region) {

    region->type = MMAP_FREE;
    mmap->free_mem += region->size;


    // А теперь наша задача если рядом есть свободные куски, то слить их воедино
    // Что бы освободить занятые записи о регионах
    if (region->next_region != 0 && region->prev_region !=0 &&
            ( (mmap_region_t *)region->next_region)->type == MMAP_FREE && ( (mmap_region_t *)region->prev_region)->type == MMAP_FREE) {
        // С обоих сторон свободное пространство
        ( (mmap_region_t *)region->prev_region)->size += region->size + ( (mmap_region_t *)region->next_region)->size;
        ((mmap_region_t *)region->prev_region)->next_region = ( (mmap_region_t *)region->next_region)->next_region;
        ( (mmap_region_t *)( (mmap_region_t *)region->prev_region)->next_region)->prev_region = region->prev_region;
        memset(region->next_region, 0, sizeof(mmap_region_t));
        memset(region, 0, sizeof(mmap_region_t));
        mmap->regions_count -= 2;
    } else if (region->next_region != 0 && ( (mmap_region_t *)region->next_region)->type == MMAP_FREE) {
        if (region->prev_region != 0) {
            ( (mmap_region_t *)region->next_region)->prev_region = region->prev_region;
            ( (mmap_region_t *)region->prev_region)->next_region = region->next_region;
        } else {
            ( (mmap_region_t *)region->next_region)->prev_region = 0;
            mmap->first_region = region->next_region;
        }
        ( (mmap_region_t *)region->next_region)->size += region->size;
        memset(region, 0, sizeof(mmap_region_t));
        mmap->regions_count--;
    } else if (region->prev_region != 0 && ( (mmap_region_t *)region->prev_region)->type == MMAP_FREE) {
        if (region->next_region != 0) {
            ( (mmap_region_t *)region->prev_region)->next_region = region->next_region;
            ( (mmap_region_t *)region->prev_region)->prev_region = region->prev_region;
        } else {
            ( (mmap_region_t *)region->prev_region)->next_region = 0;
            mmap->last_region = region->prev_region;
        }
        ( (mmap_region_t *)region->prev_region)->size += region->size;
        memset(region, 0, sizeof(mmap_region_t));
        mmap->regions_count--;
    }

    mmap->version++;
    return 1;
}

/*
 * Поиск самого низкого свободного региона определённого размера
 * Если указать size = 0 то вернёт любой самый низкий свободный регион
 * Если ошибка, то вернёт <0, если нет то результат будет в параметре region
 */
int mmap_find_lowest_free_region(mmap_t *mmap, phys_bytes size, mmap_region_t *region) {
    mmap_region_t *lowest = 0;

    for (int i = 0; i < mmap->regions_allocated; i++) {
        if (mmap->regions[i].type == MMAP_FREE) {
            if (mmap->regions[i].size >= size) {
                if (lowest == 0) {
                    lowest = &mmap->regions[i];
                } else if (lowest->start > mmap->regions[i].start) {
                    lowest = &mmap->regions[i];
                }
            }
        }
    }

    if (lowest == 0) {
        return MMAP_ERROR_NOT_AVALIBLE_REGIONS;
    }

    memcpy(lowest, region, sizeof(mmap_region_t));
    region->size = size;

    return 1;
}

int mmap_find_lowest_free_aligned_region(mmap_t *mmap, phys_bytes size, phys_bytes align, mmap_region_t *region) {
    mmap_region_t *lowest = 0;

    phys_bytes aligned_start;

    for (int i = 0; i < mmap->regions_allocated; i++) {
        if (mmap->regions[i].type == MMAP_FREE) {
            aligned_start = (mmap->regions[i].start + align) & ~align;
            if (mmap->regions[i].size + (aligned_start - mmap->regions[i].start) >= size + (aligned_start - mmap->regions[i].start)) {
                if (lowest == 0) {
                    lowest = &mmap->regions[i];
                } else if (lowest->start > mmap->regions[i].start) {
                    lowest = &mmap->regions[i];
                }
            }
        }
    }


    if (lowest == 0) {
        return MMAP_ERROR_NOT_AVALIBLE_REGIONS;
    }

    memcpy(lowest, region, sizeof(mmap_region_t));
    region->start = aligned_start;
    region->size = size;

    return 1;
}

int mmap_copy_to_new_location(mmap_t *mmap, mmap_t *new_mmap) {
    mmap_region_t *iter;
    uint32_t first = 1;
    int regions = 0;

    new_mmap->free_mem = mmap->free_mem;
    new_mmap->total_mem = mmap->total_mem;
    new_mmap->version = mmap->version;
    new_mmap->l1_page_size = mmap->l1_page_size;
    new_mmap->regions_count = mmap->regions_count;

    for (iter = (mmap_region_t *) mmap->first_region; iter != 0; iter = (mmap_region_t *) iter->next_region) {
        mmap_region_t *new_region;
        mmap_find_undef_region_record(new_mmap, new_region);
        memcpy((void *)iter, (void *) new_region, sizeof(mmap_region_t));
        if (first) {
            new_mmap->first_region = new_region;
            new_mmap->last_region = new_region;
            new_region->prev_region = 0;
            first = 0;
        } else {
            ( (mmap_region_t *)new_mmap->last_region)->next_region = new_region;
            new_region->prev_region = new_mmap->last_region;
            new_mmap->last_region = new_region;
        }
        regions++;
    }


    return regions;
}