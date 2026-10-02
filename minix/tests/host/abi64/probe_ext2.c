/*
 * abi64 probe: ext2 on-disk structures (server-internal headers, included
 * from the tree).  The super block is on disk up to s_reserved; the
 * in-memory fields after it may change with the width.
 */
#define ABI64_PROBE
#include "abi64.h"

#include <minix/drivers.h>
#include <minix/fsdriver.h>

#define EXTERN extern
#include <minix/fs/ext2/const.h>
#include <minix/fs/ext2/type.h>
#include <minix/fs/ext2/super.h>

ABI64_TYPE("ext", d_inode);
ABI64_FIELD("ext", d_inode, i_block);
ABI64_FIELD("ext", d_inode, osd2);
ABI64_TYPE("ext", struct ext2_disk_dir_desc);
ABI64_TYPE("ext", struct group_desc);
typedef struct super_block ext2_super_block;
ABI64_FIELD("ext", ext2_super_block, s_first_meta_bg);
ABI64_DISKEND(ext2_super_block, s_reserved);
