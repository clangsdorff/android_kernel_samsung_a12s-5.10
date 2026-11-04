/* camera_module_t of the a12s legacy HAL (0x168) laid out as the A14 HWL expects (0x188) */
#include <dlfcn.h>
#include <string.h>

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

/* camera3_device_ops: 10 hooks + reserved[8], copied with two spare slots */
#define OPS_SLOTS 20
#define OPS_CONFIGURE_STREAMS 1
#define DEVICE_OPS 120

typedef int (*configure_streams_t)(void *dev, struct stream_config *cfg);
typedef int (*open_t)(const void *module, const char *id, void **dev);

static const void *(*hal_methods)[1];
static configure_streams_t hal_configure_streams;
static void *shim_ops[OPS_SLOTS];

static int shim_configure_streams(void *dev, struct stream_config *cfg)
{
    for (unsigned int i = 0; cfg && i < cfg->num_streams; i++) {
        char *s = cfg->streams[i];
        int opt;
        memcpy(&opt, s + A14_STREAM_OPTION, sizeof(opt));
        memcpy(s + A12S_STREAM_OPTION, &opt, sizeof(opt));
    }
    return hal_configure_streams(dev, cfg);
}

static int shim_open(const void *module, const char *id, void **dev)
{
    int ret = ((open_t)(*hal_methods)[0])(module, id, dev);
    if (ret || !*dev)
        return ret;
    void ***ops = (void ***)((char *)*dev + DEVICE_OPS);
    if (!hal_configure_streams) {
        memcpy(shim_ops, *ops, sizeof(shim_ops));
        hal_configure_streams = (configure_streams_t)shim_ops[OPS_CONFIGURE_STREAMS];
        shim_ops[OPS_CONFIGURE_STREAMS] = (void *)shim_configure_streams;
    }
    *ops = shim_ops;
    return 0;
}

static const void *shim_methods[1] = { (const void *)shim_open };

__attribute__((constructor)) static void camera_shim_init(void)
{
    void *h = dlopen(A12S_HAL, RTLD_NOW);
    const struct a12s_module *m = h ? dlsym(h, "HMI") : NULL;
    if (!m)
        return;
    void *dso = HMI.common.dso;
    HMI.common = m->common;
    HMI.common.dso = dso;
    hal_methods = m->common.methods;
    HMI.common.methods = (void *)shim_methods;
    HMI.get_number_of_cameras = m->get_number_of_cameras;
    HMI.get_camera_info = m->get_camera_info;
    HMI.set_callbacks = m->set_callbacks;
    HMI.get_vendor_tag_ops = m->get_vendor_tag_ops;
    HMI.open_legacy = m->open_legacy;
    HMI.set_torch_mode = m->set_torch_mode;
    HMI.init = m->init;
    HMI.get_physical_camera_info = m->get_physical_camera_info;
    HMI.is_stream_combination_supported = m->is_stream_combination_supported;
    HMI.notify_device_state_change = m->notify_device_state_change;
    HMI.set_torch_mode_strength = m->set_torch_mode_strength;
}
