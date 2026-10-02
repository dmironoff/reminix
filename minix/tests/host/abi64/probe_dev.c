/*
 * abi64 probe: structures that devices read or write in memory (DMA
 * descriptors, rings, request headers), from driver and library headers.
 * Structures defined inside driver .c files are out of reach here;
 * docs/types-audit.md lists them.
 */
#define ABI64_PROBE
#include "abi64.h"

#include <minix/drivers.h>
#include <minix/lib/libvirtio/virtio_ring.h>
#include <minix/drivers/net/e1000/e1000_hw.h>
#include <minix/drivers/storage/virtio_blk/virtio_blk.h>
#include <minix/drivers/net/virtio_net/virtio_net.h>

/* virtio 0.9.5 split ring (legacy): the device reads/writes these. */
ABI64_TYPE("ext", struct vring_desc);
ABI64_FIELD("ext", struct vring_desc, len);
ABI64_FIELD("ext", struct vring_desc, next);
ABI64_TYPE("ext", struct vring_used_elem);
ABI64_OFFSET("ext", struct vring_avail, ring);
ABI64_OFFSET("ext", struct vring_used, ring);
ABI64_TYPE("ext", struct virtio_blk_outhdr);
ABI64_FIELD("ext", struct virtio_blk_outhdr, sector);
ABI64_TYPE("ext", struct virtio_blk_config);
ABI64_TYPE("ext", struct virtio_net_hdr);

/* Intel 8254x legacy descriptors. */
ABI64_TYPE("ext", e1000_rx_desc_t);
ABI64_TYPE("ext", e1000_tx_desc_t);
