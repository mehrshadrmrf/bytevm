#include <stddef.h>
#include <string.h>

#include "bytevm/memory.h"
#include "test_framework.h"

int test_memory(void)
{
    int failures = 0;
    Memory mem;

    /* mem_init zeroes the entire 64 KB space. */
    memset(mem.data, 0xAA, sizeof(mem.data));
    mem_init(&mem);
    {
        int all_zero = 1;
        size_t i;
        for (i = 0; i < MEMORY_SIZE; i++) {
            if (mem.data[i] != 0) {
                all_zero = 0;
                break;
            }
        }
        TEST_ASSERT(all_zero, "mem_init zeroes all 65536 bytes", &failures);
    }

    /* mem_write8 / mem_read8 round-trip. */
    mem_write8(&mem, 0x1234, 0x42);
    TEST_ASSERT_EQ_U16(mem_read8(&mem, 0x1234), 0x42, "mem_read8 returns what mem_write8 wrote",
                       &failures);

    /* mem_write16 / mem_read16 round-trip, little-endian byte order. */
    mem_write16(&mem, 0x2000, 0xBEEF);
    TEST_ASSERT_EQ_U16(mem.data[0x2000], 0xEF, "mem_write16 stores the low byte first (LE)",
                       &failures);
    TEST_ASSERT_EQ_U16(mem.data[0x2001], 0xBE, "mem_write16 stores the high byte second (LE)",
                       &failures);
    TEST_ASSERT_EQ_U16(mem_read16(&mem, 0x2000), 0xBEEF, "mem_read16 round-trips mem_write16",
                       &failures);

    /* Address wraparound at the top of the 64 KB space (DESIGN.md section 3):
     * a 16-bit access at 0xFFFF must wrap its second byte to address 0x0000. */
    mem_init(&mem);
    mem_write16(&mem, 0xFFFF, 0xABCD);
    TEST_ASSERT_EQ_U16(mem.data[0xFFFF], 0xCD,
                       "mem_write16 at 0xFFFF writes the low byte at 0xFFFF", &failures);
    TEST_ASSERT_EQ_U16(mem.data[0x0000], 0xAB,
                       "mem_write16 at 0xFFFF wraps the high byte to 0x0000", &failures);
    TEST_ASSERT_EQ_U16(mem_read16(&mem, 0xFFFF), 0xABCD,
                       "mem_read16 at 0xFFFF wraps consistently with mem_write16", &failures);

    return failures;
}
