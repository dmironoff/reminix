//
// Created by dmironov on 19.03.2026.
//
#include <sys/types.h>
#include <machine/cpu.h>
#include <minix/type.h>
#include <minix/board.h>
#include <io.h>

#include "kernel/kernel.h"
#include "kernel/proc.h"
#include "kernel/vm.h"
#include "kernel/proto.h"
#include "arch_proto.h"
#include "bsp_devices_mmap.h"


void bsp_devices_mmap (bsp_devices_mmap_t *mmap, uint32_t   *count) {

    mmap[0].start = 0x0;
    mmap[0].size = 0x40000000;
    *count = 1;

}