#ifndef KUI_C_EXAMPLE_COMMON_H
#define KUI_C_EXAMPLE_COMMON_H

#include <kui_c.h>

#include <stdio.h>

#define KUI_EXAMPLE_CHECK(expression)                                      \
    do {                                                                    \
        KuiStatus kui_example_status_ = (expression);                       \
        if (kui_example_status_ != KUI_OK) {                                \
            fprintf(stderr, "FAILED: %s -> %s (%d)\n", #expression,       \
                    kui_status_message(kui_example_status_),                \
                    (int)kui_example_status_);                              \
            return 1;                                                       \
        }                                                                   \
    } while (0)

#endif
