#ifndef BYTEVM_TEST_FRAMEWORK_H
#define BYTEVM_TEST_FRAMEWORK_H

#include <stdint.h>
#include <stdio.h>

/* Minimal, header-only test assertion macros. No external framework
 * dependency (DESIGN.md section 14).
 *
 * Each tests/unit/test_<module>.c exposes a plain function, e.g.
 *
 *     int test_module(void) {
 *         int failures = 0;
 *         TEST_ASSERT(condition, "message", &failures);
 *         return failures;
 *     }
 *
 * It must NOT define main() itself. tests/test_main.c is the only file
 * that defines main(); it calls each test_<module>() and sums the
 * returned failure counts. (Linking more than one file that defines its
 * own main() into the same binary fails at link time -- this shape is
 * how that's avoided.) */

#define TEST_ASSERT(cond, msg, failcount_ptr)                                                    \
    do {                                                                                         \
        if (!(cond)) {                                                                           \
            fprintf(stderr, "  FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__);                  \
            (*(failcount_ptr))++;                                                                \
        }                                                                                        \
    } while (0)

#define TEST_ASSERT_EQ_U16(actual, expected, msg, failcount_ptr)                                 \
    do {                                                                                         \
        uint16_t test_actual_   = (uint16_t)(actual);                                            \
        uint16_t test_expected_ = (uint16_t)(expected);                                          \
        if (test_actual_ != test_expected_) {                                                    \
            fprintf(stderr, "  FAIL: %s (%s:%d) -- got 0x%04X, want 0x%04X\n", (msg), __FILE__,  \
                    __LINE__, test_actual_, test_expected_);                                     \
            (*(failcount_ptr))++;                                                                \
        }                                                                                        \
    } while (0)

#endif /* BYTEVM_TEST_FRAMEWORK_H */
