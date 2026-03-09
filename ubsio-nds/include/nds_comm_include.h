#ifndef __NDS_COMM_INCLUDE_H__
#define __NDS_COMM_INCLUDE_H__
#include <linux/types.h>
#include <asm/ioctl.h>
#ifndef __KERNEL__
#define u64 uint64_t
#define s64 int64_t
#define u8 uint8_t
#define u32 uint32_t
#endif

#define DEV_MEM_SIZE 1024 * 1024 * 1024 * 2
struct nds_mem_find_info {
    u64 devaddr;
    u64 cpuvaddr;
    u64 len;
    bool found;
};

struct nds_dev_info_s {
    u64 dev_id;
} __attribute__((packed, aligned(8)));
typedef struct nds_dev_info_s nds_dev_info_t;

struct nds_ioctl_map_s {
    struct nds_dev_info_s dev;
    u64 c_vaddr;
    u64 c_size;
    u64 n_vaddr;
    u64 n_size;
    u64 end_addr;
    u32 sbuf_block;
} __attribute__((packed, aligned(8)));
typedef struct nds_ioctl_map_s nds_ioctl_map_t;

struct nds_ioctl_io_s {
    u64 cpuvaddr;         /* cpu vaddr */
    loff_t offset;        // file offset.
    u64 size;             // Read/Write length
    u64 end_fence_value;  // End fence value for DMA completion
    s64 ioctl_return;
    int fd;  // File descriptor
} __attribute__((packed, aligned(8)));
typedef struct nds_ioctl_io_s nds_ioctl_io_t;

struct nds_ioctl_ret_s {
    s64 ret;
    u8 padding[40];
} __attribute__((packed, aligned(8)));

typedef struct nds_ioctl_ret_s nds_ioctl_ret_t;

union nds_ioctl_para_s {
    struct nds_ioctl_map_s map_param;
    struct nds_ioctl_io_s io_para;
    struct nds_ioctl_ret_s ret;
} __attribute__((packed, aligned(8)));
typedef union nds_ioctl_para_s nds_ioctl_para_t;

#define NDS_IOCTL 0x88 /* 0x4c */
#define NDS_IOCTL_MAP _IOW(NDS_IOCTL, 1, struct nds_ioctl_map_s)
#define NDS_IOCTL_READ _IOW(NDS_IOCTL, 2, struct nds_ioctl_io_s)
#define NDS_IOCTL_WRITE _IOW(NDS_IOCTL, 3, struct nds_ioctl_io_s)
#define NDS_IOCTL_UNMAP _IOW(NDS_IOCTL, 4, struct nds_ioctl_map_s)

#endif

