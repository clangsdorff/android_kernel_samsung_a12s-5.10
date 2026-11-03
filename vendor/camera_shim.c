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

__attribute__((constructor)) static void camera_shim_init(void)
{
    void *h = dlopen(A12S_HAL, RTLD_NOW);
    const struct a12s_module *m = h ? dlsym(h, "HMI") : NULL;
    if (!m)
        return;
    void *dso = HMI.common.dso;
    HMI.common = m->common;
    HMI.common.dso = dso;
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
