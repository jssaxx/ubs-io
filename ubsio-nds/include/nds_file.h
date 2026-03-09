#ifndef __NDS_FILE_H__
#define __NDS_FILE_H__
#include <stdint.h>
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "nds_comm_include.h"

#define IO_URING_MAX_DEPTH 4096

typedef struct nds_fileid {
    int fd;
    int deviceID;
} nds_fileid_t;

typedef struct nds_phony_node_s {
    void **vaddrs;
    size_t mmap_count;
    uint64_t n_addr;
    size_t length;
    size_t length_left;  // 最后一次mmap的长度
    bool has_reg;

    // uint64_t ref;
    // pthread_mutex_t ref_lock; // 用于ref的互斥锁, 防止该node在使用中被其他线程释放

    struct nds_phony_node_s *next;  // 指向下一个节点的指针
} nds_p2p_map_t;

typedef struct nds_phony_buffer_s {
    nds_fileid_t fid;
    int bdev_fd;
    bool init_stat;
    struct nds_phony_node_s *head;
    pthread_mutex_t lock;  // 用于保护链表的互斥锁
    bool using_iouring;
} nds_phony_buffer_t;

int nds_init(int deviceID);        // 初始化指定设备的直通
int nds_init_async(int deviceID); // 初始化指定设备的直通和异步传输队列
int nds_uninit();

int nds_open(const char *path, int oflag, ...);  // 打开指定文件

int nds_regmem(nds_fileid_t fid, const void *addr, size_t len);    // 将设备虚拟地址注册进直通模块
int nds_unregmem(nds_fileid_t fid, const void *addr, size_t len);  // 将设备虚拟地址解除直通注册

ssize_t nds_read(nds_fileid_t fid, void *buf, off_t buf_offset, size_t nbyte, off_t f_offset);   // 同步读取
ssize_t nds_readv_batch(nds_fileid_t fid, struct iovec *iovs, size_t iov_cnt, off_t f_offset, size_t ring_id = 0);
#endif

