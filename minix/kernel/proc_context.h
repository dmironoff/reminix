//
// Created by dmironov on 04.04.2026.
//

#ifndef REMINIX_PROC_CONTEXT_H
#define REMINIX_PROC_CONTEXT_H

/*
 * Архитектурно независимая структура для хранения номеров контекста для ASID/PCID
 */
typedef struct {
    uint32_t generation;
    uint32_t id;
} proc_context_id_t;


#endif //REMINIX_PROC_CONTEXT_H
