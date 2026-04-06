//
// Created by dmironov on 02.04.2026.
//

#ifndef REMINIX_REUP_H
#define REMINIX_REUP_H

#include "arch_configs.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <minix/endpoint.h>
#include <machine/vmparam.h>
#include <minix/u64.h>
#include <minix/board.h>
#include <sys/reboot.h>
#include "clock.h"
#include "direct_utils.h"
#include "hw_intr.h"
#include "arch_proto.h"
#include "kernel/env_params_utils.h"
#include "kernel/bootstrap_kernel_information.h"
#include "kernel/apt_utils.h"
#include "kernel/mmap_utils.h"
#include "kernel/resmp.h"
#include "kernel/reup.h"
#include "spinlock.h"

#endif //REMINIX_REUP_H
