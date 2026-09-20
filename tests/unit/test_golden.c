#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bytevm/assembler.h"
#include "bytevm/loader.h"
#include "bytevm/vm.h"
#include "test_framework.h"

/* Assumes the test binary runs with the project root as the current
 * directory -- true for `make test` (the Makefile never `cd`s), which is
 * the only way this suite is meant to be invoked. */
#define GOLDEN_STEP_LIMIT 1000000

typedef struct {
    const char *name;
    const char *asm_path;
    const char *expected_stdout_path;
    const char *expected_exit_path;
} GoldenCase;

static const GoldenCase GOLDEN_CASES[] = {
    {"hello", "examples/hello.asm", "tests/golden/hello.expected", "tests/golden/hello.exit"},
    {"factorial", "examples/factorial.asm", "tests/golden/factorial.expected",
     "tests/golden/factorial.exit"},
    {"sum", "examples/sum.asm", "tests/golden/sum.expected", "tests/golden/sum.exit"},
    {"sort", "examples/sort.asm", "tests/golden/sort.expected", "tests/golden/sort.exit"},
};
#define NUM_GOLDEN_CASES (sizeof(GOLDEN_CASES) / sizeof(GOLDEN_CASES[0]))

static char *read_whole_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    if (len < 0) {
        fclose(f);
        return NULL;
    }
    char *buf = malloc((size_t)len + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[got] = '\0';
    if (out_len != NULL) {
        *out_len = got;
    }
    return buf;
}

static void run_golden_case(const GoldenCase *tc, int *failures)
{
    char *source = read_whole_file(tc->asm_path, NULL);
    if (source == NULL) {
        fprintf(stderr, "  (could not read %s)\n", tc->asm_path);
        TEST_ASSERT(false, tc->name, failures);
        return;
    }

    AssemblyResult asm_result;
    assemble(source, &asm_result);
    free(source);
    if (!asm_result.ok) {
        fprintf(stderr, "  (%s failed to assemble: %s)\n", tc->asm_path, asm_result.error);
        TEST_ASSERT(false, tc->name, failures);
        assembly_result_free(&asm_result);
        return;
    }

    VM vm;
    vm_init(&vm);
    VmResult load = vm_load(&vm, asm_result.image, asm_result.image_size);
    assembly_result_free(&asm_result);
    if (load != VM_OK) {
        fprintf(stderr, "  (%s failed to load: %s)\n", tc->asm_path, vm_result_str(load));
        TEST_ASSERT(false, tc->name, failures);
        return;
    }

    /* Capture SYS PRINT_INT/PRINT_CHAR output by redirecting the actual
     * stdout stream (same approach as test_functions.c) -- vm.c's
     * syscalls write to the real stdout, not a passed-in FILE*. */
    FILE *tmp = tmpfile();
    if (tmp == NULL) {
        TEST_ASSERT(false, tc->name, failures);
        return;
    }
    FILE *saved_stdout = stdout;
    stdout = tmp;

    long steps = 0;
    while (!vm.halted && steps < GOLDEN_STEP_LIMIT) {
        vm_step(&vm);
        steps++;
    }

    fflush(tmp);
    stdout = saved_stdout;

    char actual_stdout[512] = {0};
    rewind(tmp);
    fread(actual_stdout, 1, sizeof(actual_stdout) - 1, tmp);
    fclose(tmp);

    if (vm.last_error != VM_OK) {
        fprintf(stderr, "  (%s halted with an error: %s)\n", tc->asm_path,
                vm_result_str(vm.last_error));
    }
    char test_name[128];
    snprintf(test_name, sizeof(test_name), "%s halts without error", tc->name);
    TEST_ASSERT(vm.last_error == VM_OK, test_name, failures);

    char *expected_stdout = read_whole_file(tc->expected_stdout_path, NULL);
    char *expected_exit = read_whole_file(tc->expected_exit_path, NULL);

    snprintf(test_name, sizeof(test_name), "%s stdout matches tests/golden/", tc->name);
    bool stdout_matches = expected_stdout != NULL && strcmp(actual_stdout, expected_stdout) == 0;
    if (!stdout_matches) {
        fprintf(stderr, "  (%s: got %zu bytes, expected file %s%s)\n", tc->name,
                strlen(actual_stdout), tc->expected_stdout_path,
                expected_stdout == NULL ? " (missing)" : "");
    }
    TEST_ASSERT(stdout_matches, test_name, failures);

    int actual_exit = (int)(vm.cpu.r[0] & 0xFFu);
    int expected_exit_code = expected_exit != NULL ? atoi(expected_exit) : -1;
    snprintf(test_name, sizeof(test_name), "%s exit code matches tests/golden/", tc->name);
    TEST_ASSERT(actual_exit == expected_exit_code, test_name, failures);

    free(expected_stdout);
    free(expected_exit);
}

int test_golden(void)
{
    int failures = 0;
    size_t i;
    for (i = 0; i < NUM_GOLDEN_CASES; i++) {
        run_golden_case(&GOLDEN_CASES[i], &failures);
    }
    return failures;
}
