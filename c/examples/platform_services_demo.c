#include "example_common.h"

int main(void) {
    char os_name[128];
    char os_version[128];
    size_t length = 0;
    size_t monitor_count = 0;
    float physical_width = 0.0f;
    float physical_height = 0.0f;

    KUI_EXAMPLE_CHECK(kui_platform_get_os_name(
        os_name, sizeof os_name, &length));
    KUI_EXAMPLE_CHECK(kui_platform_get_os_version(
        os_version, sizeof os_version, &length));
    KUI_EXAMPLE_CHECK(kui_platform_get_monitor_count(&monitor_count));
    KUI_EXAMPLE_CHECK(kui_platform_get_primary_monitor_physical_size(
        &physical_width, &physical_height));

    printf("OS: %s %s\n", os_name, os_version);
    printf("Monitors: %zu, primary physical size: %.1f x %.1f logical units\n",
           monitor_count, physical_width, physical_height);
    return 0;
}
