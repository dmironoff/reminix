//
// Created by dmironov on 19.03.2026.
//

#include <sys/types.h>
#include <minix/endpoint.h>
#include "string.h"
#include <minix/physmemorymap.h>
#include "mmap_utils.h"

/*
 * Один из двух основных механизма ядра - абстрактная карта памяти
 * Сюда мы вносим информацию обо всём использовании физической памяти
 * Так же VM будет следить за количеством использующих эти регионы процессов
 */


/*
 * Поиск следующей нераспределённой записи
 * в буффере регионов памяти
 */
static int mmap_find_undef_region_record(mmap_t *mmap, mmap_region_t *region) {
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
 * Порезать регион памяти
 * Новый регион памяти наследует все параметры от родителя
 */
static int biteoff(mmap_t *mmap, mmap_region_t *from, phys_bytes start, phys_bytes size, mmap_region_t *new) {

    int res = 0;

    if (start % mmap->l2_page_size || size % mmap->l2_page_size) {
        return MMAP_ALIGNMENT_ERROR;
    }

    if (start + size > from->start + from->size) {
        return MMAP_ERROR_INCORRECT_ADDR;
    }

    if (from->start == start) {
        // Мы в начале региона
        if (size == from->size) {
            // Очень странная хуита, но ладно
            // Мы хотим новый кусок такой же как старый
            new = from;
        } else {
            // Мы меньше исходного региона
            res = mmap_find_undef_region_record(mmap, new);
            if (res <= 0) {
                return res;
            }
            new->start = start;
            new->size = size;
            from->start += size;
            from->size -= size;
            new->flags = from->flags;
            new->cache_hint = from->cache_hint;
            new->type = from->type;
            if (from->prev != 0) {
                ((mmap_region_t *)from->prev)->next = (void *) new;
                new->prev = from->prev;
            }
            if (mmap->first_region == from) {
                mmap->first_region = new;
            }
            from->prev = (void *) new;
            new->next = (void *) from;
            mmap->regions_count++;
        }
    } else {
        // Начало в середине
        res = mmap_find_undef_region_record(mmap, new);
        if (res <= 0) {
            return res;
        }
        if (start + size == from->start + from->size) {
            // Мы в самом конце
            from->size -= size;
            new->start = start;
            new->size = size;
            new->flags = from->flags;
            new->type = from->type;
            new->cache_hint = from->cache_hint;
            if (from->next != 0) {
                ((mmap_region_t *)from->next)->prev = (void *) new;
                new->next = from->next;
            }
            if (mmap->last_region == from) {
                mmap->last_region = new;
            }
            from->next = (void *)new;
            new->prev = (void *)from;
            mmap->regions_count++;
        } else {
            // Мы посередине и бьём регион на три части
            mmap_region_t *new_next;
            res = mmap_find_undef_region_record(mmap, new_next);
            if (res <= 0) {
                return res;
            }
            new_next->start = start + size;
            new_next->size = from->start + from->size - start + size;
            from->size = from->size - new_next->size - size;
            new->start = start;
            new->size = size;
            new->flags = from->flags;
            new->cache_hint = from->cache_hint;
            new->type = from->type;
            new_next->flags = from->flags;
            new_next->cache_hint = from->cache_hint;
            new_next->type = from->type;
            if (from->next != 0) {
                ((mmap_region_t *)from->next)->prev = (void *) new_next;
                new_next->next = from->next;
            }
            new->next = (void *) new_next;
            new_next->prev = (void *) new;
            if (mmap->last_region == from) {
                mmap->last_region = new_next;
            }
            from->next = (void *) new;
            new->prev = (void *) from;
            mmap->regions_count += 2;
        }
    }

    return 1;
}

/*
 * Соединить два региона в один, освободившаяся запись возвращается в пулл
 * Тип, флаги и т.д. наследуются от первого региона
 * Регионы должны быть выставлены по адресам - сначала первый, потом второй
 *
 */
static int concat(mmap_t *mmap, mmap_region_t *first, mmap_region_t *second) {
    first->size += second->size;
    if (second->next != 0) {
        ((mmap_region_t *)second->next)->prev = (void *) first;
    }
    if (mmap->last_region == second) {
        mmap->last_region = first;
    }
    first->next = second->next;
    mmap->regions_count--;
    memset((void *) second, 0, sizeof(mmap_region_t));
    return 1;
}


/*
 * Поиск региона памяти по адресу памяти
 * Возвращает указатель на регион памяти в карте через переменную region
 */
int mmap_find_region_by_addr(mmap_t *mmap, phys_bytes addr, mmap_region_t *region) {
    mmap_region_t *iter;
    for (iter = mmap->first_region; iter != 0; iter = (mmap_region_t *)iter->next) {
        if (iter->start <= addr && (iter->start + iter->size) > addr) {
            region = iter;
            return 1;
        }
    }

    return MMAP_ERROR_INCORRECT_ADDR;
}


/*
 * Инициализация чистой карты памяти
 * Предполагает, что указатель на массив свободных записей уже установлен
 *
 */
int mmap_init(mmap_t *mmap, phys_bytes l2_page_size, phys_bytes size) {
    int res;
    mmap_region_t *region;

    if (size % l2_page_size) {
        return MMAP_ALIGNMENT_ERROR;
    }

    res = mmap_find_undef_region_record(mmap, region);
    if (res <= 0) {
        return res;
    }
    region->flags = 0;
    region->cache_hint = 0;
    region->type = MMAP_FREE;
    region->size = size;
    mmap->regions_count = 1;
    mmap->first_region = region;
    mmap->last_region = region;
    mmap->free_mem = size;
    mmap->total_mem = size;
    mmap->l2_page_size = l2_page_size;
    mmap->version = 0;

    return 1;
}

/*
 * Выделение памяти для устройств
 * Если попадает на свободную память, то порежет регион и уменьшие данные о свободной памяти
 * На свободную память эта функция попадает только при инициализации системы, когда мы имеем один большой "прото-регион"
 * Если на память устройств, то просто порежет регион - это для выделения отдельных регионов драйверам для счёта refs
 */
int mmap_alloc_device(mmap_t *mmap, phys_bytes start, phys_bytes size, mmap_region_t *region) {
    int res;
    mmap_region_t *working_region;

    if (start % mmap->l2_page_size || size % mmap->l2_page_size) {
        return MMAP_ALIGNMENT_ERROR;
    }

    res = mmap_find_region_by_addr(mmap, start, working_region);
    if (res <= 0) {
        return res;
    }
    if (working_region->type == MMAP_DEVICE) {
        if (working_region->size < size) {
            return MMAP_ERROR_INCORRECT_ADDR;
        }
        res = biteoff(mmap, working_region, start, size, region);
        if (res <= 0) {
            return res;
        }
        mmap->version++;
        return 1;
    } else if (working_region->type == MMAP_FREE) {
        if (working_region->size < size) {
            return MMAP_ERROR_INCORRECT_ADDR;
        }
        res = biteoff(mmap, working_region, start, size, region);
        if (res <= 0) {
            return res;
        }
        region->cache_hint = MMAP_CACHE_NO;
        region->type = MMAP_DEVICE;
        mmap->total_mem -= region->size;
        mmap->free_mem -= region->size;
        mmap->version++;
        return 1;
    }
    return MMAP_ERROR_REGION_BUSY;
}

/*
 * Выделение памяти для dma
 * Если попадает на свободную память, то порежет регион и уменьшие данные о свободной памяти
 * На свободную память эта функция попадает только при инициализации системы, когда мы имеем один большой "прото-регион"
 * Если на память dma, то просто порежет регион - это для выделения отдельных регионов драйверам для счёта refs
 */
int mmap_alloc_dma(mmap_t *mmap, phys_bytes start, phys_bytes size, mmap_region_t *region) {
    int res;
    mmap_region_t *working_region;

    if (start % mmap->l2_page_size || size % mmap->l2_page_size) {
        return MMAP_ALIGNMENT_ERROR;
    }

    res = mmap_find_region_by_addr(mmap, start, working_region);
    if (res <= 0) {
        return res;
    }
    if (working_region->type == MMAP_DMA) {
        if (working_region->size < size) {
            return MMAP_ERROR_INCORRECT_ADDR;
        }
        res = biteoff(mmap, working_region, start, size, region);
        if (res <= 0) {
            return res;
        }
        mmap->version++;
        return 1;
    } else if (working_region->type == MMAP_FREE) {
        if (working_region->size < size) {
            return MMAP_ERROR_INCORRECT_ADDR;
        }
        res = biteoff(mmap, working_region, start, size, region);
        if (res <= 0) {
            return res;
        }
        region->cache_hint = MMAP_CACHE_DMA;
        region->type = MMAP_DMA;
        mmap->total_mem -= region->size;
        mmap->free_mem -= region->size;
        mmap->version++;
        return 1;
    }
    return MMAP_ERROR_REGION_BUSY;
}

/*
 * Выделение памяти из "прото-региона" для зарезервированных железкой адресов
 * Функция вызывается один раз при инициализации системы
 */
int mmap_alloc_reserved(mmap_t *mmap, phys_bytes start, phys_bytes size, mmap_region_t *region) {
    int res;
    mmap_region_t *working_region;

    if (start % mmap->l2_page_size || size % mmap->l2_page_size) {
        return MMAP_ALIGNMENT_ERROR;
    }

    res = mmap_find_region_by_addr(mmap, start, working_region);
    if (res <= 0) {
        return res;
    }
    if (working_region->type == MMAP_FREE) {
        if (working_region->size < size) {
            return MMAP_ERROR_INCORRECT_ADDR;
        }
        res = biteoff(mmap, working_region, start, size, region);
        if (res <= 0) {
            return res;
        }
        region->cache_hint = MMAP_CACHE_DMA;
        region->type = MMAP_DMA;
        mmap->total_mem -= region->size;
        mmap->free_mem -= region->size;
        mmap->version++;
        return 1;
    }
    return MMAP_ERROR_REGION_BUSY;
}

/*
 * Копирование карты памяти по новому адресу
 * Используется при инициализации системы
 */
int mmap_copy_to_new_location(mmap_t *mmap, mmap_t *new_mmap) {
    mmap_region_t *iter;
    uint32_t first = 1;
    int regions = 0;

    new_mmap->free_mem = mmap->free_mem;
    new_mmap->total_mem = mmap->total_mem;
    new_mmap->version = mmap->version;
    new_mmap->l2_page_size = mmap->l2_page_size;
    new_mmap->regions_count = mmap->regions_count;

    for (iter = (mmap_region_t *) mmap->first_region; iter != 0; iter = (mmap_region_t *) iter->next) {
        mmap_region_t *new_region;
        mmap_find_undef_region_record(new_mmap, new_region);
        memcpy((void *)iter, (void *) new_region, sizeof(mmap_region_t));
        if (first) {
            new_mmap->first_region = new_region;
            new_mmap->last_region = new_region;
            new_region->prev = 0;
            first = 0;
        } else {
            ( (mmap_region_t *)new_mmap->last_region)->next = (void *) new_region;
            new_region->prev = (void *) new_mmap->last_region;
            new_mmap->last_region = new_region;
        }
        regions++;
    }

    return regions;
}

/*
 * Проверка наличия свободного места
 */
static inline int mmap_check_free_size(mmap_t *mmap, phys_bytes start, phys_bytes size) {
    mmap_region_t *iter;
    int res;

    if (start % mmap->l2_page_size || size % mmap->l2_page_size) {
        return 0;
    }

    for(res = mmap_find_region_by_addr(mmap, start, iter);
            iter != 0;
            iter = (mmap_region_t *) iter->next)  {
        if (res <= 0) {
            return res;
        }
        if (iter->type == MMAP_FREE) {
            if (start == iter->start) {
                if (iter->size <= size) {
                    return 1;
                } else {
                    size -= iter->size;
                    start += iter->size;
                }
            } else {
                if (iter->size + iter->start >= start + size) {
                    return 1;
                } else {
                    size = start + size - iter->size + iter->start;
                    start = iter->start + iter->size;
                }
            }
        } else {
            return 0;
        }
    }
    return 0;
}

/*
 * Выделение нового региона памяти
 * Возвращает указатель на структуру описывающую новый регион через последнюю переменную
 */
int mmap_alloc_region(mmap_t *mmap, phys_bytes start, phys_bytes size, mmap_region_t *region) {
    mmap_region_t *iter;
    mmap_region_t *new;
    mmap_region_t *tmp;
    int res;

    if (start % mmap->l2_page_size || size % mmap->l2_page_size) {
        return MMAP_ALLOCATED;
    }

    if (size > mmap->total_mem) {
        return MMAP_ERROR_INCORRECT_ADDR;
    }

    // Сначала прогоним проверку на свободное место, ну важно нам это прежде чем выделять всякие структуры
    if (!mmap_check_free_size(mmap, start, size)) {
        return MMAP_ERROR_REGION_BUSY;
    }

    for (res = mmap_find_region_by_addr(mmap, start, iter);
            iter != 0;
            iter = (mmap_region_t *) iter->next) {
        if (res <= 0) {
            return res;
        }
        if (iter->type == MMAP_FREE) {
            if (iter->start == start) {
                if (iter->size == size) {
                    size = 0;
                    if (new != 0) {
                        res = concat(mmap, new, iter);
                        if (res <= 0) {
                            return res;
                        }
                        region = new;
                    } else {
                        region = iter;
                    }
                } else if (iter->size > size) {
                    res = biteoff(mmap, iter, start, size, tmp);
                    if (res <= 0) {
                        return res;
                    }
                    if (new != 0) {
                        res = concat(mmap, new, tmp);
                        if (res <= 0) {
                            return res;
                        }
                        region = new;
                    } else {
                        region = tmp;
                    }
                    size = 0;
                } else {
                   if (new != 0) {
                       res = concat(mmap, new, iter);
                       if (res <= 0) {
                           return res;
                       }
                       iter = new;
                   } else {
                       new = iter;
                   }
                   size -= iter->size;
                   start += iter->size;
                }
            } else {
                if (iter->start + iter->size >= start + size) {
                    res = biteoff(mmap, iter, start, size, region);
                    if (res <= 0) {
                        return res;
                    }
                    start = iter->start + iter->size;
                    size = 0;
                } else {
                    res = biteoff(mmap, iter, start, iter->start + iter->size - start, new);
                    if (res <= 0) {
                        return res;
                    }
                    start = iter->start + iter->size;
                    size -= (iter->start + iter->size - start);
                    iter = new;
                }
            }
        } else {
            return MMAP_ERROR_REGION_BUSY;
        }
        if (size == 0) {
            mmap->version++;
            return 1;
        }
    }

    return MMAP_ERROR_REGION_BUSY;
}

/*
 * Выделяет самый низкий из свободных регионов памяти размеров size
 * Возвращает указатель на новый регион через переменнную
 * Адрес старта региона будет в возвращаемой указателем структуре
 */
int mmap_alloc_lowest_region(mmap_t *mmap, phys_bytes size, mmap_region_t *region) {
    phys_bytes iter_size = size;
    phys_bytes start = 0;
    int started = 0;
    mmap_region_t *iter;

    if (size % mmap->l2_page_size) {
        return MMAP_ALIGNMENT_ERROR;
    }

    if (size > mmap->total_mem) {
        return MMAP_ERROR_INCORRECT_ADDR;
    }

    for (iter = mmap->first_region; iter != 0; iter = (mmap_region_t *)iter->next) {
        if (iter->type == MMAP_FREE) {
            if (!started) {
                started = 1;
                start = iter->start;
            }
            if (iter->size <= iter_size) {
                iter_size -= iter->size;
            } else {
                iter_size = 0;
            }
        } else {
            started = 0;
            iter_size = size;
            start = 0;
        }
        if (started && iter_size == 0) {
            return mmap_alloc_region(mmap, start, size, region);
        }
    }

    return MMAP_ERROR_REGION_BUSY;
}

/*
 * Выделяет самый высокий из свободных регионов памяти размеров size
 * Возвращает указатель на новый регион через переменнную
 * Адрес старта региона будет в возвращаемой указателем структуре
 */
int mmap_alloc_highest_region(mmap_t *mmap, phys_bytes size, mmap_region_t *region) {
    phys_bytes iter_size = size;
    phys_bytes start = 0;
    int started = 0;
    mmap_region_t *iter;

    if (size % mmap->l2_page_size) {
        return MMAP_ALIGNMENT_ERROR;
    }

    if (size > mmap->total_mem) {
        return MMAP_ERROR_INCORRECT_ADDR;
    }

    for (iter = mmap->last_region; iter != 0; iter = (mmap_region_t *)iter->prev) {
        if (iter->type == MMAP_FREE) {
            if (!started) {
                started = 1;
            }
            if (iter_size < iter->size) {
                start = iter->start + iter->size - iter_size;
                iter_size = 0;
            } else if (iter_size >= iter->size) {
                start = iter->start;
                iter_size -= iter->size;
            }
        } else {
            started = 0;
            iter_size = size;
            start = 0;
        }
        if (started && iter_size == 0) {
            return mmap_alloc_region(mmap, start, size, region);
        }
    }

    return MMAP_ERROR_REGION_BUSY;
}

/*
 * Освободить память
 * Ядро может освободить любую память кроме DEVICE, DMA и RESERVED
 * Свободные регионы памяти автоматически объеденияются
 * start и size могут попадать на середину занятых регионов, но всегда должны быть выровнены по l2_page_size
 * Если область залезает на MMAP_FREE, то это никак не влияет на работу функции
 */
int mmap_free_memory(mmap_t *mmap, phys_bytes start, phys_bytes size) {
    int res = 0;
    mmap_region_t  *iter;
    mmap_region_t  *free_region;
    mmap_region_t  *tmp;

    if (start % mmap->l2_page_size || size % mmap->l2_page_size) {
        return MMAP_ALIGNMENT_ERROR;
    }

    if (size > mmap->total_mem) {
        return MMAP_ERROR_INCORRECT_ADDR;
    }

    for (res = mmap_find_region_by_addr(mmap, start, iter); iter != 0; iter = (mmap_region_t *)iter->next) {
        if (res <= 0) {
            return res;
        }
        if (iter->type == MMAP_DMA || iter->type == MMAP_DEVICE || iter->type == MMAP_RESERVED) {
            // Если прилетели в область где размечены неубиваемые области, то просто вылетаем с ошибкой
            // Вызывать на правильных областях - это задача используюущего этот апи программиста
            return MMAP_ERROR_INCORRECT_ADDR;
        }
        if (start == iter->start) {
             // Начало совпало с регионом
             if (iter->size >= size) {
                 // Область умещается в регион
                 res = biteoff(mmap, iter, start, size, tmp);
                 if (res <= 0) {
                     return res;
                 }
                 if (free_region == 0) {
                     free_region = tmp;
                 } else {
                     res = concat(mmap, free_region, tmp);
                     if (res <= 0) {
                         return res;
                     }
                 }
                 size = 0;
             } else {
                 // Регион меньше освобождаемой области
                 if (free_region == 0) {
                    free_region = iter;
                 } else {
                     res = concat(mmap, free_region, iter);
                     if (res <= 0) {
                         return res;
                     }
                     iter = free_region;
                 }
                 size -= iter->size;
                 start = iter->start + iter->size;
             }
        } else {
            // Начало в середине региона
            // Это может произойти только если функция только начала свою работу
            if (iter->start + iter->size <= start + size) {
                // Мы помещаемся в регионе
                res = biteoff(mmap, iter, start, size, free_region);
                size = 0;
            } else {
                // Мы не помещаемся в регионе
                free_region = iter;
                size -= iter->size;
                start = iter->start + iter->size;
            }
        }

        if (size == 0) {
            free_region->type = MMAP_FREE;
            free_region->cache_hint = 0;

            if (((mmap_region_t *)free_region->next)->type == MMAP_FREE) {
                // Если следующий регион свободен, то мы их сольём в один;
                res = concat(mmap, free_region, (mmap_region_t *)free_region->next);
                if (res <= 0) {
                    return res;
                }
            }

            if (((mmap_region_t *)free_region->prev)->type == MMAP_FREE) {
                // Если предыдущий регион свободен, то мы их сольём в один;
                res = concat(mmap, (mmap_region_t *)free_region->prev, free_region);
                if (res <= 0) {
                    return res;
                }
            }

            mmap->version++;
            return 1;
        }
    }

    if (res <= 0) {
        return res;
    }

    return 1;
}

/*
 * Функция для итерации регионов по типу
 * Никаких проверок выравнивания, просто: 0 - ненайдено, 1 - найден регион
 */
int mmap_find_next_by_type(mmap_t *mmap, mmap_type_t type, phys_bytes offset, mmap_region_t *region) {
    mmap_region_t  *iter;
    int res;

    for (res = mmap_find_region_by_addr(mmap, offset, iter); iter != 0; iter = (mmap_region_t *) iter->next) {
        if (res <= 0) {
            return 0;
        }
        if (iter->type == type) {
            region = iter;
            return 1;
        }
    }

    return 0;
}

/*
 * Выравнивание адреса или размера по странице l2
 */
phys_bytes mmap_align(mmap_t *mmap, phys_bytes value) {
    return (value + mmap->l2_page_size) & (~mmap->l2_page_size);
}