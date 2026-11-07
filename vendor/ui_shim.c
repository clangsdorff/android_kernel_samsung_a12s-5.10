// The a12s camera HAL calls GraphicBufferMapper::unlock(buffer_handle_t), which the A14 libui no longer exports;
// it is rebuilt on top of unlock(buffer_handle_t, base::unique_fd*) the way the old libui did it.
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <unistd.h>

struct native_handle;

int _ZN7android19GraphicBufferMapper6unlockEPK13native_handlePNS_4base14unique_fd_implINS4_13DefaultCloserEEE(
        void *mapper, const struct native_handle *handle, int *fence);

__attribute__((visibility("default")))
int _ZN7android19GraphicBufferMapper6unlockEPK13native_handle(void *mapper, const struct native_handle *handle)
{
    int fence = -1;
    int ret = _ZN7android19GraphicBufferMapper6unlockEPK13native_handlePNS_4base14unique_fd_implINS4_13DefaultCloserEEE(
            mapper, handle, &fence);

    if (fence >= 0) {
        struct pollfd pfd = { .fd = fence, .events = POLLIN };
        while (poll(&pfd, 1, -1) < 0 && errno == EINTR)
            ;
        close(fence);
    }
    return ret;
}

// In 5.10 /dev/ion is replaced by dma-buf heaps (/dev/dma_heap/system).
// libexynoscamera3.so calls legacy ion_open, ion_alloc_fd, ion_close for 48MP remosaic.
#ifndef DMA_HEAP_IOC_MAGIC
struct dma_heap_allocation_data {
    uint64_t len;
    uint32_t fd;
    uint32_t fd_flags;
    uint64_t heap_flags;
};
#define DMA_HEAP_IOC_MAGIC 'H'
#define DMA_HEAP_IOCTL_ALLOC _IOWR(DMA_HEAP_IOC_MAGIC, 0x0, struct dma_heap_allocation_data)
#endif

__attribute__((visibility("default")))
int ion_open(void)
{
    int fd = open("/dev/dma_heap/system", O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return -errno;
    return fd;
}

__attribute__((visibility("default")))
int ion_close(int fd)
{
    if (fd >= 0)
        close(fd);
    return 0;
}

__attribute__((visibility("default")))
int ion_alloc_fd(int fd, size_t len, size_t align, unsigned int heap_mask, unsigned int flags, int *handle_fd)
{
    (void)align;
    (void)heap_mask;
    (void)flags;
    if (fd < 0 || !handle_fd)
        return -EINVAL;

    struct dma_heap_allocation_data data = {
        .len = (uint64_t)len,
        .fd_flags = O_RDWR | O_CLOEXEC,
        .heap_flags = 0,
    };
    int ret = ioctl(fd, DMA_HEAP_IOCTL_ALLOC, &data);
    if (ret < 0)
        return -errno;

    *handle_fd = (int)data.fd;
    return 0;
}
