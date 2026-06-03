// npu_driver.h - Host CPU Bare-metal Driver API for NPU

#ifndef NPU_DRIVER_H
#define NPU_DRIVER_H

#include "npu_mmio.h"
#include <stddef.h>
#include <stdbool.h>

// Initialize the NPU subsystem and configure Mailbox connections.
// Returns 0 on success.
int npu_init(void);

// Simple contiguous memory allocator for placing tensors in DDR.
// In a real OS this would wrap `kmalloc` or `dma_alloc_coherent`.
void* npu_alloc(size_t size);
void npu_free(void* ptr);

// Initialize a Command Queue at the specified DDR memory location.
npu_cmd_queue_t* npu_queue_create(void* mem, uint32_t capacity);

// Enqueue a micro-coded command to the queue.
// If the queue is full, this function will block (or return false if non-blocking).
bool npu_enqueue(npu_cmd_queue_t* q, const npu_cmd_t* cmd);

// Ring the NPU doorbell to wake up the RISC-V firmware, notifying it of new commands.
void npu_ring_doorbell(uint16_t cluster_id);

// Block the Host CPU until the specified cluster signals completion.
// Returns the status code from the NPU (0 for success).
int npu_wait_cluster(uint16_t cluster_id);

// Broadcast doorbell to all clusters
void npu_ring_doorbell_all(void);

// Block until all clusters signal completion
int npu_wait_all(void);

#endif // NPU_DRIVER_H
