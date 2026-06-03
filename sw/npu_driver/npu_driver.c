// npu_driver.c - Host CPU Bare-metal Driver API for NPU

#include "npu_driver.h"

// Simple external allocator state
static uint32_t current_heap_ptr = NPU_DDR_MEMORY_BASE;

int npu_init(void) {
    // Reset allocator
    current_heap_ptr = NPU_DDR_MEMORY_BASE;
    
    // Clear mailboxes for all clusters
    for (int i = 0; i < 4; i++) {
        volatile uint32_t* mbox = (volatile uint32_t*)(NPU_MBOX_BASE + i * 0x1000 + MBOX_TRIGGER_OFFSET);
        *mbox = 0;
    }
    
    return 0; // Success
}

void* npu_alloc(size_t size) {
    // 32-byte align the allocation size for DMA efficiency
    size = (size + 31) & ~31;
    void* ptr = (void*)(uintptr_t)current_heap_ptr;
    current_heap_ptr += size;
    return ptr;
}

void npu_free(void* ptr) {
    // Bare-metal allocator doesn't truly free in this simple testbench
    // Can just reset heap via npu_init() between tests
    (void)ptr; 
}

npu_cmd_queue_t* npu_queue_create(void* mem, uint32_t capacity) {
    npu_cmd_queue_t* q = (npu_cmd_queue_t*)mem;
    q->head = 0;
    q->tail = 0;
    q->capacity = capacity;
    q->status_flags = 0;
    return q;
}

bool npu_enqueue(npu_cmd_queue_t* q, const npu_cmd_t* cmd) {
    uint32_t next_tail = (q->tail + 1) % q->capacity;
    
    // Check if full (tail + 1 == head)
    // Note: since this is run on a simulated host with cache-coherency ignored for now,
    // we use a volatile read.
    volatile uint32_t head = q->head;
    if (next_tail == head) {
        return false; // Queue full
    }
    
    // Copy command to queue
    q->cmds[q->tail] = *cmd;
    
    // Memory barrier would go here on a real processor (e.g. `__sync_synchronize()`)
    
    // Update tail
    q->tail = next_tail;
    
    return true;
}

void npu_ring_doorbell(uint16_t cluster_id) {
    if (cluster_id >= 4) return;
    volatile uint32_t* mbox = (volatile uint32_t*)(NPU_MBOX_BASE + cluster_id * 0x1000 + MBOX_TRIGGER_OFFSET);
    // Write 1 to trigger the interrupt/WFI wakeup
    *mbox = 1;
}

void npu_ring_doorbell_all(void) {
    for (int i = 0; i < 4; i++) {
        npu_ring_doorbell(i);
    }
}

int npu_wait_cluster(uint16_t cluster_id) {
    if (cluster_id >= 4) return -1;
    
    volatile uint32_t* mbox = (volatile uint32_t*)(NPU_MBOX_BASE + cluster_id * 0x1000 + MBOX_TRIGGER_OFFSET);
    
    // Poll the mailbox (NPU firmware writes status codes here, usually >= 2 indicates completion)
    // 0xA5A5 typically indicates SUCCESS, while 2 is "Done".
    while (true) {
        uint32_t val = *mbox;
        if (val == 2 || val == 0xA5A5) {
            *mbox = 0; // Acknowledge and clear
            return 0;  // Success
        } else if (val == 0xDEAD || val == 0xDEADBEEF) {
            *mbox = 0; // Acknowledge and clear
            return -1; // Error
        }
        // In a real system, we'd use WFI and rely on the interrupt handler
    }
}

int npu_wait_all(void) {
    int ret = 0;
    for (int i = 0; i < 4; i++) {
        int r = npu_wait_cluster(i);
        if (r != 0) ret = r;
    }
    return ret;
}
