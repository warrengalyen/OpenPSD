#include "openpsd_tests.h"

#include <stdio.h>

int main(void)
{
    int failures = 0;

    failures += run_basic_tests();
    failures += run_background_layer_tests();
    failures += run_text_layer_tests();
    failures += run_color_mode_tests();

    if (failures == 0) {
        printf("\n========================================\n");
        printf("OpenPSD: ALL TESTS PASSED\n");
        printf("========================================\n");
    } else {
        printf("\n========================================\n");
        printf("OpenPSD: TESTS FAILED (%d failure group(s))\n", failures);
        printf("========================================\n");
    }

    return (failures == 0) ? 0 : 1;
}

