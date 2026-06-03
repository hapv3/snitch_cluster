// host_main.c - Main entry point for the Host CPU Firmware
#include "../npu_driver/npu_driver.h"

int main(void) {
    // 1. Initialize NPU Driver
    npu_init();
    
    // 2. Allocate memory for Command Queue (say, at 0x80000000)
    // We will place queue for Cluster 0 at 0x80000000
    npu_cmd_queue_t* q0 = npu_queue_create((void*)0x80000000, 64);
    
    // 3. Create a test sequence for Cluster 0
    npu_cmd_t cmd1;
    cmd1.opcode = OP_COMPUTE_CONV2D;
    cmd1.cluster_id = 0;
    cmd1.flags = 0;
    cmd1.args.compute.act_addr = 0x10000000;
    cmd1.args.compute.wgt_addr = 0x10000200;
    cmd1.args.compute.out_addr = 0x10000400;
    
    npu_cmd_t cmd2;
    cmd2.opcode = OP_FINISH;
    cmd2.cluster_id = 0;
    cmd2.flags = 0;
    
    // 4. Enqueue commands
    npu_enqueue(q0, &cmd1);
    npu_enqueue(q0, &cmd2);
    
    // 5. Wake up Cluster 0
    npu_ring_doorbell(0);
    
    // 6. Wait for Cluster 0 to finish
    int res = npu_wait_cluster(0);
    
    // If we reach here with res == 0, success!
    // Signal testbench (we use mailbox 0 trigger as a proxy to testbench)
    if (res == 0) {
        volatile uint32_t* mbox = (volatile uint32_t*)(NPU_MBOX_BASE + MBOX_TRIGGER_OFFSET);
        *mbox = 0x12345678; // Custom success code for host
    }
    
    while(1) {
        __asm__ volatile("wfi");
    }
    return 0;
}
