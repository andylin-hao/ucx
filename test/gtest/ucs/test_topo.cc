/**
* Copyright (C) Mellanox Technologies Ltd. 2001-2019.  ALL RIGHTS RESERVED.
*
* See file LICENSE for terms.
*/

#include <common/test.h>
extern "C" {
#include <ucs/sys/topo.h>
}

class test_topo : public ucs::test {
};

UCS_TEST_F(test_topo, find_device_by_bus_id) {
    ucs_status_t status;
    ucs_sys_device_t dev1;
    ucs_sys_device_t dev2;
    ucs_sys_bus_id_t dummy_bus_id;

    dummy_bus_id.domain   = 0xffff;
    dummy_bus_id.bus      = 0xff;
    dummy_bus_id.slot     = 0xff;
    dummy_bus_id.function = 1;

    status = ucs_topo_find_device_by_bus_id(&dummy_bus_id, &dev1);
    ASSERT_UCS_OK(status);

    dummy_bus_id.function = 2;

    status = ucs_topo_find_device_by_bus_id(&dummy_bus_id, &dev2);
    ASSERT_UCS_OK(status);
    ASSERT_EQ(dev2, ((unsigned)dev1 + 1));
}

UCS_TEST_F(test_topo, get_distance) {
    ucs_status_t status;
    ucs_sys_dev_distance_t distance;

    status = ucs_topo_get_distance(UCS_SYS_DEVICE_ID_UNKNOWN,
                                   UCS_SYS_DEVICE_ID_UNKNOWN, &distance);
    ASSERT_EQ(UCS_OK, status);
    EXPECT_NEAR(distance.latency, 0.0, 1e-9);

    char buf[128];
    UCS_TEST_MESSAGE << "distance: "
                     << ucs_topo_distance_str(&distance, buf, sizeof(buf));
}

UCS_TEST_F(test_topo, print_info) {
    ucs_topo_print_info(NULL);
}

UCS_TEST_F(test_topo, bdf_name) {
    static const char *bdf_name = "0002:8f:5c.0";
    ucs_sys_device_t sys_dev    = UCS_SYS_DEVICE_ID_UNKNOWN;

    ucs_status_t status = ucs_topo_find_device_by_bdf_name(bdf_name, &sys_dev);
    ASSERT_UCS_OK(status);
    ASSERT_NE(UCS_SYS_DEVICE_ID_UNKNOWN, sys_dev);

    char name_buffer[64];
    const char *found_name = ucs_topo_sys_device_bdf_name(sys_dev, name_buffer,
                                                          sizeof(name_buffer));
    ASSERT_UCS_OK(status);
    EXPECT_EQ(std::string(bdf_name), std::string(found_name));
}

UCS_TEST_F(test_topo, bdf_name_zero_domain) {
    static const char *bdf_name = "0000:8f:5c.0";
    ucs_sys_device_t sys_dev    = UCS_SYS_DEVICE_ID_UNKNOWN;

    const char *short_bdf = strchr(bdf_name, ':') + 1;
    ucs_status_t status = ucs_topo_find_device_by_bdf_name(short_bdf, &sys_dev);
    ASSERT_UCS_OK(status);
    ASSERT_NE(UCS_SYS_DEVICE_ID_UNKNOWN, sys_dev);

    char name_buffer[64];
    const char *found_name = ucs_topo_sys_device_bdf_name(sys_dev, name_buffer,
                                                          sizeof(name_buffer));
    ASSERT_UCS_OK(status);
    EXPECT_EQ(std::string(bdf_name), std::string(found_name));
}

UCS_TEST_F(test_topo, bdf_name_invalid) {
    ucs_sys_device_t sys_dev = UCS_SYS_DEVICE_ID_UNKNOWN;
    ucs_status_t status;

    status = ucs_topo_find_device_by_bdf_name("0000:8f:5c!0", &sys_dev);
    EXPECT_EQ(UCS_ERR_INVALID_PARAM, status);

    status = ucs_topo_find_device_by_bdf_name("0000:8t:5c.0", &sys_dev);
    EXPECT_EQ(UCS_ERR_INVALID_PARAM, status);

    status = ucs_topo_find_device_by_bdf_name("5c.0", &sys_dev);
    EXPECT_EQ(UCS_ERR_INVALID_PARAM, status);

    status = ucs_topo_find_device_by_bdf_name("1:2:3", &sys_dev);
    EXPECT_EQ(UCS_ERR_INVALID_PARAM, status);
}
