/* camera_module_t of the a12s legacy HAL (0x168) laid out as the A14 HWL expects (0x188) */
#include <android/log.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define LOG_TAG "camera_shim"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define A12S_HAL "/vendor/lib64/hw/camera.exynos850.so"

struct hw_module {
    unsigned long long head;
    void *ptr[3];
    void *methods;
    void *dso;
    unsigned long long reserved[25];
};

struct a12s_module {
    struct hw_module common;
    void *get_number_of_cameras, *get_camera_info, *set_callbacks, *get_vendor_tag_ops, *open_legacy,
         *set_torch_mode, *init, *get_physical_camera_info, *is_stream_combination_supported,
         *notify_device_state_change, *set_torch_mode_strength, *reserved[3];
};

struct a14_module {
    struct hw_module common;
    void *get_number_of_cameras, *get_camera_info, *get_session_characteristics,
         *construct_default_request_settings, *set_callbacks, *get_vendor_tag_ops, *open_legacy,
         *set_torch_mode, *init, *get_physical_camera_info, *is_stream_combination_supported,
         *notify_device_state_change, *reserved[2], *set_torch_mode_strength,
         *get_concurrent_streaming_camera_ids, *is_concurrent_stream_combination_supported, *reserved2;
};

_Static_assert(sizeof(struct hw_module) == 248, "hw_module_t");
_Static_assert(sizeof(struct a12s_module) == 0x168, "a12s HMI");
_Static_assert(sizeof(struct a14_module) == 0x188, "A14 HMI");

__attribute__((visibility("default"))) struct a14_module HMI;

/* a12s HAL reads the Samsung stream option at +0x38; the A14 HWL keeps it in bits 16+ of +0x50 */
#define A12S_STREAM_OPTION 0x38
#define A14_STREAM_OPTION 0x52

struct stream_config {
    unsigned int num_streams;
    char **streams;
    unsigned int operation_mode;
    const void *session_parameters;
};

#define OPS_SLOTS 20
#define OPS_CONFIGURE_STREAMS 1
#define DEVICE_CLOSE 112
#define DEVICE_OPS 120

typedef int (*configure_streams_t)(void *dev, struct stream_config *cfg);
typedef int (*open_t)(const void *module, const char *id, void **dev);
typedef int (*close_t)(void *dev);

/* Legacy HAL handle and original module pointer */
static void *g_hal_handle = NULL;
static const struct a12s_module *g_orig_module = NULL;
static const void *(*g_hal_methods)[1] = NULL;

/* Per-device instance tracker to prevent static state leakage */
typedef struct device_node {
    void *dev;
    close_t orig_close;
    configure_streams_t orig_configure_streams;
    void *ops[OPS_SLOTS];
    struct device_node *next;
} device_node_t;

static device_node_t *g_device_list = NULL;
static pthread_mutex_t g_device_lock = PTHREAD_MUTEX_INITIALIZER;

static device_node_t *find_device_node(void *dev)
{
    device_node_t *curr = g_device_list;
    while (curr) {
        if (curr->dev == dev)
            return curr;
        curr = curr->next;
    }
    return NULL;
}

static device_node_t *remove_device_node(void *dev)
{
    device_node_t **pp = &g_device_list;
    while (*pp) {
        if ((*pp)->dev == dev) {
            device_node_t *found = *pp;
            *pp = found->next;
            return found;
        }
        pp = &(*pp)->next;
    }
    return NULL;
}

static int shim_close(void *dev)
{
    pthread_mutex_lock(&g_device_lock);
    device_node_t *node = remove_device_node(dev);
    pthread_mutex_unlock(&g_device_lock);

    if (!node) {
        ALOGE("shim_close called on untracked device %p", dev);
        return -1;
    }

    close_t orig_close = node->orig_close;
    free(node); /* Clean up per-device heap allocation */

    int ret = orig_close(dev);
    ALOGI("close %p ret %d", dev, ret);
    return ret;
}

static int shim_configure_streams(void *dev, struct stream_config *cfg)
{
    pthread_mutex_lock(&g_device_lock);
    device_node_t *node = find_device_node(dev);
    pthread_mutex_unlock(&g_device_lock);

    if (!node || !node->orig_configure_streams) {
        ALOGE("shim_configure_streams: untracked device %p", dev);
        return -1;
    }

    /* Perform Samsung stream option bit translation */
    for (unsigned int i = 0; cfg && i < cfg->num_streams; i++) {
        char *s = cfg->streams[i];
        if (s) {
            int opt;
            memcpy(&opt, s + A14_STREAM_OPTION, sizeof(opt));
            memcpy(s + A12S_STREAM_OPTION, &opt, sizeof(opt));
        }
    }

    return node->orig_configure_streams(dev, cfg);
}

static int shim_open(const void *module, const char *id, void **dev)
{
    /* Fix 1: Pass original g_orig_module pointer to prevent structure offset corruption */
    const void *target_module = g_orig_module ? (const void *)g_orig_module : module;
    open_t orig_open = (open_t)(*g_hal_methods)[0];

    int ret = orig_open(target_module, id, dev);
    if (ret || !*dev)
        return ret;

    /* Fix 2: Allocate per-device tracking node instead of sharing global static buffers */
    device_node_t *node = (device_node_t *)calloc(1, sizeof(device_node_t));
    if (!node) {
        ALOGE("Failed to allocate per-device shim node");
        return -1;
    }
    node->dev = *dev;

    /* Copy and hook device ops table per instance */
    void ***ops_ptr = (void ***)((char *)*dev + DEVICE_OPS);
    if (ops_ptr && *ops_ptr) {
        memcpy(node->ops, *ops_ptr, sizeof(node->ops));
        node->orig_configure_streams = (configure_streams_t)node->ops[OPS_CONFIGURE_STREAMS];
        node->ops[OPS_CONFIGURE_STREAMS] = (void *)shim_configure_streams;
        *ops_ptr = node->ops;
    }

    /* Hook device close function per instance */
    close_t *close_ptr = (close_t *)((char *)*dev + DEVICE_CLOSE);
    if (close_ptr) {
        node->orig_close = *close_ptr;
        *close_ptr = shim_close;
    }

    /* Insert into device list under lock */
    pthread_mutex_lock(&g_device_lock);
    node->next = g_device_list;
    g_device_list = node;
    pthread_mutex_unlock(&g_device_lock);

    ALOGI("open %s %p", id, *dev);
    return 0;
}

static const void *shim_methods[1] = { (const void *)shim_open };

__attribute__((constructor)) static void camera_shim_init(void)
{
    g_hal_handle = dlopen(A12S_HAL, RTLD_NOW);
    if (!g_hal_handle) {
        ALOGE("Failed to dlopen %s: %s", A12S_HAL, dlerror());
        return;
    }

    g_orig_module = (const struct a12s_module *)dlsym(g_hal_handle, "HMI");
    if (!g_orig_module) {
        ALOGE("Failed to find HMI in %s", A12S_HAL);
        return;
    }

    void *dso = HMI.common.dso;
    HMI.common = g_orig_module->common;
    HMI.common.dso = dso;

    g_hal_methods = (const void *(*)[1])g_orig_module->common.methods;
    HMI.common.methods = (void *)shim_methods;

    HMI.get_number_of_cameras = g_orig_module->get_number_of_cameras;
    HMI.get_camera_info = g_orig_module->get_camera_info;
    HMI.set_callbacks = g_orig_module->set_callbacks;
    HMI.get_vendor_tag_ops = g_orig_module->get_vendor_tag_ops;
    HMI.open_legacy = g_orig_module->open_legacy;
    HMI.set_torch_mode = g_orig_module->set_torch_mode;
    HMI.init = g_orig_module->init;
    HMI.get_physical_camera_info = g_orig_module->get_physical_camera_info;
    HMI.is_stream_combination_supported = g_orig_module->is_stream_combination_supported;
    HMI.notify_device_state_change = g_orig_module->notify_device_state_change;
    HMI.set_torch_mode_strength = g_orig_module->set_torch_mode_strength;
}