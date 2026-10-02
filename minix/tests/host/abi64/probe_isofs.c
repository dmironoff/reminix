/*
 * abi64 probe: ISO 9660 primary volume descriptor (isofs, read from disk
 * with one memcpy of 2048 bytes; in-memory fields follow).
 */
#define ABI64_PROBE
#include "abi64.h"

#include <minix/fs/isofs/inc.h>

ABI64_FIELD("ext", struct iso9660_vol_pri_desc, root_directory);
ABI64_FIELD("ext", struct iso9660_vol_pri_desc, application_use);
ABI64_DISKEND(struct iso9660_vol_pri_desc, reserved2);
