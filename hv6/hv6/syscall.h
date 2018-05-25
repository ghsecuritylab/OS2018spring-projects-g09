#pragma once

#include <uapi/syscall.h>

#define SYS_map_page_desc 3
#define SYS_map_pml4 4
#define SYS_map_proc 5
#define SYS_map_dev 6
#define SYS_map_file 7
#define SYS_clone 10
#define SYS_set_proc_name 11
#define SYS_set_runnable 12
#define SYS_switch 13
#define SYS_kill 14
#define SYS_reap 16
#define SYS_reparent 17
#define SYS_alloc_pdpt 20
#define SYS_alloc_pd 21
#define SYS_alloc_pt 22
#define SYS_alloc_frame 23
#define SYS_copy_frame 24
#define SYS_protect_frame 25
#define SYS_free_pdpt 26
#define SYS_free_pd 27
#define SYS_free_pt 28
#define SYS_free_frame 29
#define SYS_reclaim_page 30
#define SYS_create 34
#define SYS_close 35
#define SYS_dup 36
#define SYS_dup2 37
#define SYS_lseek 38
#define SYS_send 40
#define SYS_recv 41
#define SYS_reply_wait 42
#define SYS_call 43
#define SYS_map_pcipage 46
#define SYS_alloc_iommu_root 47
#define SYS_alloc_iommu_pdpt 48
#define SYS_alloc_iommu_pd 49
#define SYS_alloc_iommu_pt 50
#define SYS_alloc_iommu_frame 51
#define SYS_map_iommu_frame 52
#define SYS_reclaim_iommu_frame 53
#define SYS_reclaim_iommu_root 54
#define SYS_alloc_vector 55
#define SYS_reclaim_vector 56
#define SYS_alloc_intremap 57
#define SYS_reclaim_intremap 58
#define SYS_ack_intr 59
#define SYS_alloc_io_bitmap 70
#define SYS_alloc_port 71
#define SYS_reclaim_port 72
#define SYS_mmap_page 80
#define SYS_munmap_page 81
#define SYS_debug_exit 123
#define SYS_debug_print_console 124
#define SYS_debug_print_screen 125
///#define SYS_debug_dmesg 126
///#define SYS_debug_sysctl 127
#define SYS_map_page 126
#define SYS_debug_getchar 127

#ifndef __ASSEMBLER__

// called by kernel
void init_syscalls();

extern void *syscalls[];

#endif
