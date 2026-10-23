#include "virtio.h"
#include "paging.h"
#include "pmm.h"
#include "kheap.h"
#include <string.h>

/* Helper to allocate and initialize a virtqueue */
int virtqueue_setup(virtqueue_t *vq, uint16_t num) {
    if (num == 0 || (num & (num - 1)) != 0) return -1; /* Must be power of 2 */

    size_t desc_size  = sizeof(virtq_desc_t) * num;
    size_t avail_size = sizeof(virtq_avail_t) + sizeof(uint16_t) * num + sizeof(uint16_t);
    size_t used_size  = sizeof(virtq_used_t) + sizeof(virtq_used_item_t) * num + sizeof(uint16_t);

    /* Allocate memory for the rings. Using kalloc for simplicity, 
       but Virtio 1.0 requires these to be physically contiguous or at least 
       accessible by the device. Since kalloc uses a contiguous physical chunk 
       in this kernel, it should be fine. */
    
    vq->desc  = (virtq_desc_t *)kalloc(desc_size);
    vq->avail = (virtq_avail_t *)kalloc(avail_size);
    vq->used  = (virtq_used_t *)kalloc(used_size);

    if (!vq->desc || !vq->avail || !vq->used) {
        if (vq->desc) kfree(vq->desc);
        if (vq->avail) kfree(vq->avail);
        if (vq->used) kfree(vq->used);
        return -1;
    }

    memset(vq->desc, 0, desc_size);
    memset(vq->avail, 0, avail_size);
    memset(vq->used, 0, used_size);

    vq->num = num;
    vq->last_used_idx = 0;
    vq->free_head = 0;
    vq->num_free = num;

    /* Link descriptors in a free list */
    for (uint16_t i = 0; i < num - 1; ++i) {
        vq->desc[i].next = (uint16_t)(i + 1);
        vq->desc[i].flags = VIRTQ_DESC_F_NEXT;
    }
    vq->desc[num - 1].next = 0;
    vq->desc[num - 1].flags = 0;

    return 0;
}

uintptr_t virtio_get_phys(void *virt) {
    /* In this kernel, kernel heap is identity mapped (higher half offset handled by paging)
       Wait, let's check paging.c again. 
       paging_alloc_pages maps phys to phys. 
       kheap_init uses a physical address. 
       So virt == phys for kernel heap in this simple kernel? 
       Actually, KERNEL_VMA_BASE is 0xFFFF800000000000. 
    */
    uintptr_t v = (uintptr_t)virt;
    if (v >= 0xFFFF800000000000ULL) {
        return v - 0xFFFF800000000000ULL;
    }
    return v;
}
