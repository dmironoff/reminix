//
// Created by dmironov on 22.04.2026.
//

/*
 * Это наша часть отвечающая за маппинг адресов для драйверов внутри ядра
 */


/* list of requested physical mapping */
static kern_phys_map *kern_phys_map_head;

static int usermapped_glo_index = -1,
        usermapped_index = -1, first_um_idx = -1;

extern char usermapped_start, usermapped_end, usermapped_nonglo_start;

int arch_phys_map(const int index, phys_bytes *addr,
                  phys_bytes *len, int *flags)
{
    static int first = 1;
    kern_phys_map *phys_maps;
    int freeidx = 0;
    u32_t glo_len = (u32_t)&usermapped_nonglo_start - (u32_t)&usermapped_start;

    if (first) {
        memset(&minix_kerninfo, 0, sizeof(minix_kerninfo));
        if (glo_len > 0)      usermapped_glo_index = freeidx++;
        usermapped_index = freeidx++;
        first_um_idx     = (usermapped_glo_index != -1) ?
                           usermapped_glo_index : usermapped_index;
        first = 0;
        phys_maps = kern_phys_map_head;
        while (phys_maps) { phys_maps->index = freeidx++; phys_maps = phys_maps->next; }
    }

    if (index == usermapped_glo_index) {
        *addr = vir2phys(&usermapped_start); *len = glo_len;
        *flags = VMMF_USER | VMMF_GLO; return OK;
    }
    if (index == usermapped_index) {
        *addr  = vir2phys(&usermapped_nonglo_start);
        *len   = (u32_t)&usermapped_end - (u32_t)&usermapped_nonglo_start;
        *flags = VMMF_USER; return OK;
    }

    phys_maps = kern_phys_map_head;
    while (phys_maps) {
        if (phys_maps->index == index) {
            *addr = phys_maps->addr; *len = phys_maps->size;
            *flags = phys_maps->vm_flags; return OK;
        }
        phys_maps = phys_maps->next;
    }
    return EINVAL;
}

int arch_phys_map_reply(const int index, const vir_bytes addr)
{
    kern_phys_map *phys_maps;

    if (index == first_um_idx) {
        u32_t usermapped_offset = addr - (u32_t)&usermapped_start;
#define FIXEDPTR(ptr) (void *)((u32_t)(ptr) + usermapped_offset)
#define FIXPTR(ptr)   ptr = FIXEDPTR(ptr)
#define ASSIGN(s)     minix_kerninfo.s = FIXEDPTR(&s)
        ASSIGN(kinfo); ASSIGN(machine); ASSIGN(kmessages); ASSIGN(loadinfo);
        ASSIGN(kuserinfo); ASSIGN(arm_frclock); ASSIGN(kclockinfo);
        minix_kerninfo.kerninfo_magic      = KERNINFO_MAGIC;
        minix_kerninfo.minix_feature_flags = minix_feature_flags;
        minix_kerninfo_user = (vir_bytes)FIXEDPTR(&minix_kerninfo);
        minix_kerninfo.ki_flags |= MINIX_KIF_USERINFO;
        return OK;
    }
    if (index == usermapped_index) return OK;

    phys_maps = kern_phys_map_head;
    while (phys_maps) {
        if (phys_maps->index == index) {
            assert(phys_maps->cb != NULL);
            phys_maps->vir = addr;
            return OK;
        }
        phys_maps = phys_maps->next;
    }
    return EINVAL;
}



int kern_req_phys_map(phys_bytes base_address, vir_bytes io_size,
                      int vm_flags, kern_phys_map *priv,
                      kern_phys_map_mapped cb, vir_bytes id)
{
    assert(base_address != 0);
    assert(io_size % ARM_PAGE_SIZE == 0);
    assert(cb != NULL);
    priv->addr = base_address; priv->size = io_size;
    priv->vm_flags = vm_flags; priv->cb = cb;
    priv->id = id; priv->index = -1; priv->next = NULL;
    if (kern_phys_map_head == NULL) {
        kern_phys_map_head = priv; kern_phys_map_head->next = NULL;
    } else {
        priv->next = kern_phys_map_head; kern_phys_map_head = priv;
    }
    return 0;
}

int kern_phys_map_mapped_ptr(vir_bytes id, phys_bytes address)
{
    *((vir_bytes *)id) = address;
    return 0;
}

int kern_phys_map_ptr(phys_bytes base_address, vir_bytes io_size,
                      int vm_flags, kern_phys_map *priv, vir_bytes ptr)
{
    return kern_req_phys_map(base_address, io_size, vm_flags, priv,
                             kern_phys_map_mapped_ptr, ptr);
}