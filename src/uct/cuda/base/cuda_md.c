/**
 * Copyright (c) NVIDIA CORPORATION & AFFILIATES, 2018-2019. ALL RIGHTS RESERVED.
 * See file LICENSE for terms.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "cuda_md.h"
#include "cuda_iface.h"

#include <ucs/sys/module.h>
#include <ucs/sys/string.h>
#include <cuda.h>


void uct_cuda_base_get_sys_dev(int cuda_device,
                               ucs_sys_device_t *sys_dev_p)
{
    ucs_sys_bus_id_t bus_id;
    cudaError_t cuda_err;
    int attrib;
    ucs_status_t status;

    /* PCI domain id */
    cuda_err = cudaDeviceGetAttribute(&attrib, cudaDevAttrPciDomainId,
                                  cuda_device);
    if (cuda_err != cudaSuccess) {
        goto err;
    }
    bus_id.domain = (uint16_t)attrib;

    /* PCI bus id */
    cuda_err = cudaDeviceGetAttribute(&attrib, cudaDevAttrPciBusId,
                                  cuda_device);
    if (cuda_err != cudaSuccess) {
        goto err;
    }
    bus_id.bus = (uint8_t)attrib;

    /* PCI slot id */
    cuda_err = cudaDeviceGetAttribute(&attrib, cudaDevAttrPciDeviceId,
                                  cuda_device);
    if (cuda_err != cudaSuccess) {
        goto err;
    }
    bus_id.slot = (uint8_t)attrib;

    /* Function - always 0 */
    bus_id.function = 0;

    status = ucs_topo_find_device_by_bus_id(&bus_id, sys_dev_p);
    if (status != UCS_OK) {
        goto err;
    }

    return;

err:
    *sys_dev_p = UCS_SYS_DEVICE_ID_UNKNOWN;
}

ucs_status_t
uct_cuda_base_query_md_resources(uct_component_t *component,
                                 uct_md_resource_desc_t **resources_p,
                                 unsigned *num_resources_p)
{
    const unsigned sys_device_priority = 10;
    ucs_sys_device_t sys_dev;
    int cuda_device;
    ucs_status_t status;
    char device_name[10];
    int i, num_gpus;

    status = UCT_CUDA_FUNC(cudaGetDeviceCount(&num_gpus), UCS_LOG_LEVEL_DIAG);
    if ((status != UCS_OK) || (num_gpus == 0)) {
        return uct_md_query_empty_md_resource(resources_p, num_resources_p);
    }

    for (i = 0; i < num_gpus; ++i) {
        cuda_device = i;
        uct_cuda_base_get_sys_dev(cuda_device, &sys_dev);
        if (sys_dev == UCS_SYS_DEVICE_ID_UNKNOWN) {
            continue;
        }

        ucs_snprintf_safe(device_name, sizeof(device_name), "GPU%d",
                          cuda_device);
        status = ucs_topo_sys_device_set_name(sys_dev, device_name,
                                              sys_device_priority);
        ucs_assert_always(status == UCS_OK);
    }

    return uct_md_query_single_md_resource(component, resources_p,
                                           num_resources_p);
}

UCS_STATIC_INIT
{
    /* coverity[check_return] */
    cuInit(0);
}

UCS_STATIC_CLEANUP
{
}

UCS_MODULE_INIT() {
    /* TODO make gdrcopy independent of cuda */
    UCS_MODULE_FRAMEWORK_DECLARE(uct_cuda);
    UCS_MODULE_FRAMEWORK_LOAD(uct_cuda, 0);
    return UCS_OK;
}
