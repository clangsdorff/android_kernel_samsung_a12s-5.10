// In 5.10 /dev/ion is replaced by dma-buf heaps (/dev/dma_heap/system).
// libion.so is rebuilt on top of /dev/dma_heap/system for legacy camera HAL callers.
#include <android/log.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define LOG_TAG "ion_shim"
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

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

typedef int ion_user_handle_t;

__attribute__((visibility("default")))
int ion_open(void)
{
    int fd = open("/dev/dma_heap/system", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        ALOGE("open /dev/dma_heap/system failed: %s", strerror(errno));
        return -errno;
    }
    ALOGI("ion_open() -> fd %d", fd);
    return fd;
}

__attribute__((visibility("default")))
int ion_close(int fd)
{
    ALOGI("ion_close(fd %d)", fd);
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
    if (ret < 0) {
        ALOGE("DMA_HEAP_IOCTL_ALLOC (%zu bytes) failed: %s", len, strerror(errno));
        return -errno;
    }
    *handle_fd = (int)data.fd;
    ALOGI("ion_alloc_fd(%zu) -> fd %d", len, *handle_fd);
    return 0;
}

__attribute__((visibility("default")))
int ion_alloc(int fd, size_t len, size_t align, unsigned int heap_mask, unsigned int flags, ion_user_handle_t *handle)
{
    int handle_fd = -1;
    int ret = ion_alloc_fd(fd, len, align, heap_mask, flags, &handle_fd);
    if (ret == 0 && handle)
        *handle = handle_fd;
    return ret;
}

__attribute__((visibility("default")))
int ion_free(int fd, ion_user_handle_t handle)
{
    (void)fd;
    if (handle >= 0)
        close(handle);
    return 0;
}

__attribute__((visibility("default")))
int ion_sync_fd(int fd, int handle_fd)
{
    (void)fd;
    (void)handle_fd;
    return 0;
}

__attribute__((visibility("default")))
int ion_is_legacy(int fd)
{
    (void)fd;
    return 0;
}

__attribute__((visibility("default")))
int ion_is_using_modular_heaps(int fd)
{
    (void)fd;
    return 0;
}

__attribute__((visibility("default")))
int ion_share(int fd, ion_user_handle_t handle, int *share_fd)
{
    (void)fd;
    if (handle < 0 || !share_fd)
        return -EINVAL;
    *share_fd = dup(handle);
    return *share_fd >= 0 ? 0 : -errno;
}

__attribute__((visibility("default")))
int ion_import(int fd, int share_fd, ion_user_handle_t *handle)
{
    (void)fd;
    if (share_fd < 0 || !handle)
        return -EINVAL;
    *handle = dup(share_fd);
    return *handle >= 0 ? 0 : -errno;
}

__attribute__((visibility("default")))
int ion_map(int fd, ion_user_handle_t handle, size_t length, int prot, int flags, off_t offset,
            unsigned char **ptr, int *map_fd)
{
    (void)fd;
    (void)flags;
    (void)offset;
    if (!ptr || !map_fd || handle < 0)
        return -EINVAL;
    void *p = mmap(NULL, length, prot, MAP_SHARED, handle, 0);
    if (p == MAP_FAILED)
        return -errno;
    *ptr = (unsigned char *)p;
    *map_fd = handle;
    return 0;
}

__attribute__((visibility("default")))
int ion_query_heap_cnt(int fd, int *cnt)
{
    (void)fd;
    if (!cnt)
        return -EINVAL;
    *cnt = 1;
    return 0;
}

__attribute__((visibility("default")))
int ion_query_get_heaps(int fd, int cnt, void *buffers)
{
    (void)fd;
    (void)cnt;
    (void)buffers;
    return 0;
}
