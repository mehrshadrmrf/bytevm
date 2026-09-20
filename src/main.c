#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bytevm/assembler.h"
#include "bytevm/debugger.h"
#include "bytevm/disassembler.h"
#include "bytevm/loader.h"
#include "bytevm/vm.h"

#define MAX_STEPS 10000000

static void usage(const char *prog)
{
    fprintf(stderr,
            "ByteVM -- a 16-bit register virtual machine\n"
            "\n"
            "Usage:\n"
            "  %s run <file.asm|file.bvm>   assemble if needed, then execute\n"
            "  %s asm <file.asm> <out.bvm>  assemble to a .bvm file\n"
            "  %s dis <file.asm|file.bvm>   disassemble back to assembly text\n"
            "  %s debug <file.asm|file.bvm> step through it interactively\n",
            prog, prog, prog, prog);
}

/* Reads a whole file into a malloc'd buffer. Always NUL-terminates (one
 * byte past the reported size) so the same helper serves both .asm text
 * and binary .bvm images. */
static uint8_t *read_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "bytevm: cannot open '%s'\n", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        fprintf(stderr, "bytevm: cannot seek '%s'\n", path);
        return NULL;
    }
    long len = ftell(f);
    if (len < 0) {
        fclose(f);
        fprintf(stderr, "bytevm: cannot size '%s'\n", path);
        return NULL;
    }
    rewind(f);

    uint8_t *buf = malloc((size_t)len + 1);
    if (buf == NULL) {
        fclose(f);
        fprintf(stderr, "bytevm: out of memory reading '%s'\n", path);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[got] = '\0';
    *out_size = got;
    return buf;
}

static bool has_suffix(const char *s, const char *suffix)
{
    size_t sl = strlen(s);
    size_t xl = strlen(suffix);
    return sl >= xl && strcmp(s + sl - xl, suffix) == 0;
}

/* Produces a .bvm image from either a .asm source file (assembling it)
 * or an already-built .bvm file (using it as-is). */
static uint8_t *image_from_path(const char *path, size_t *out_size)
{
    size_t file_size;
    uint8_t *contents = read_file(path, &file_size);
    if (contents == NULL) {
        return NULL;
    }

    if (!has_suffix(path, ".asm")) {
        *out_size = file_size;
        return contents; /* treat as a raw .bvm image */
    }

    AssemblyResult result;
    assemble((const char *)contents, &result);
    free(contents);

    if (!result.ok) {
        fprintf(stderr, "%s:%s\n", path, result.error);
        assembly_result_free(&result);
        return NULL;
    }

    /* Hand the caller ownership of the image; clear it out of the result
     * so assembly_result_free doesn't take it with it. */
    uint8_t *image = result.image;
    *out_size = result.image_size;
    result.image = NULL;
    assembly_result_free(&result);
    return image;
}

static int cmd_run(const char *path)
{
    size_t image_size;
    uint8_t *image = image_from_path(path, &image_size);
    if (image == NULL) {
        return 1;
    }

    VM *vm = malloc(sizeof(VM)); /* 64KB of memory -- too big for the stack */
    if (vm == NULL) {
        free(image);
        fprintf(stderr, "bytevm: out of memory\n");
        return 1;
    }

    vm_init(vm);
    VmResult load = vm_load(vm, image, image_size);
    free(image);

    if (load != VM_OK) {
        fprintf(stderr, "bytevm: %s\n", vm_result_str(load));
        free(vm);
        return 1;
    }

    long steps = 0;
    while (!vm->halted && steps < MAX_STEPS) {
        vm_step(vm);
        steps++;
    }

    int exit_code = 0;
    if (vm->last_error != VM_OK) {
        fprintf(stderr, "bytevm: %s at PC=0x%04X\n", vm_result_str(vm->last_error), vm->error_pc);
        exit_code = 1;
    } else if (!vm->halted) {
        fprintf(stderr, "bytevm: step limit (%d) reached without halting\n", MAX_STEPS);
        exit_code = 1;
    } else {
        /* DESIGN.md section 8.7: the host exit status is R0's low byte. */
        exit_code = (int)(vm->cpu.r[0] & 0xFFu);
    }

    free(vm);
    return exit_code;
}

static int cmd_asm(const char *in_path, const char *out_path)
{
    size_t image_size;
    uint8_t *image = image_from_path(in_path, &image_size);
    if (image == NULL) {
        return 1;
    }

    FILE *out = fopen(out_path, "wb");
    if (out == NULL) {
        fprintf(stderr, "bytevm: cannot write '%s'\n", out_path);
        free(image);
        return 1;
    }
    size_t written = fwrite(image, 1, image_size, out);
    fclose(out);
    free(image);

    if (written != image_size) {
        fprintf(stderr, "bytevm: short write to '%s'\n", out_path);
        return 1;
    }
    printf("wrote %s (%zu bytes)\n", out_path, image_size);
    return 0;
}

static int cmd_dis(const char *path)
{
    size_t image_size;
    uint8_t *image = image_from_path(path, &image_size);
    if (image == NULL) {
        return 1;
    }

    DisassemblyResult result;
    disassemble(image, image_size, &result);
    free(image);

    if (!result.ok) {
        fprintf(stderr, "bytevm: %s\n", result.error);
        disassembly_result_free(&result);
        return 1;
    }

    fputs(result.text, stdout);
    disassembly_result_free(&result);
    return 0;
}

static int cmd_debug(const char *path)
{
    size_t image_size;
    uint8_t *image = image_from_path(path, &image_size);
    if (image == NULL) {
        return 1;
    }

    VM *vm = malloc(sizeof(VM));
    if (vm == NULL) {
        free(image);
        fprintf(stderr, "bytevm: out of memory\n");
        return 1;
    }

    vm_init(vm);
    VmResult load = vm_load(vm, image, image_size);
    if (load != VM_OK) {
        fprintf(stderr, "bytevm: %s\n", vm_result_str(load));
        free(vm);
        free(image);
        return 1;
    }

    Debugger dbg;
    debugger_init(&dbg, vm, image, image_size);
    debugger_repl(&dbg, stdin, stdout);

    free(vm);
    free(image);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && strcmp(argv[1], "run") == 0) {
        return cmd_run(argv[2]);
    }
    if (argc >= 4 && strcmp(argv[1], "asm") == 0) {
        return cmd_asm(argv[2], argv[3]);
    }
    if (argc >= 3 && strcmp(argv[1], "dis") == 0) {
        return cmd_dis(argv[2]);
    }
    if (argc >= 3 && strcmp(argv[1], "debug") == 0) {
        return cmd_debug(argv[2]);
    }
    usage(argv[0]);
    return 2;
}
