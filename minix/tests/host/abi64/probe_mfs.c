/*
 * abi64 probe: MFS on-disk structures (server-internal headers, included
 * from the tree).  The super block is read from disk up to s_disk_version;
 * the in-memory fields after it may change with the width, the on-disk
 * part must not.
 */
#define ABI64_PROBE
#include "abi64.h"

#include <minix/drivers.h>
#include <minix/fsdriver.h>

#define EXTERN extern
#include <minix/fs/mfs/const.h>
#include <minix/fs/mfs/type.h>
#include <minix/fs/mfs/super.h>

ABI64_TYPE("ext", d2_inode);
ABI64_FIELD("ext", d2_inode, d2_zone);
typedef struct super_block mfs_super_block;
ABI64_DISKEND(mfs_super_block, s_disk_version);
