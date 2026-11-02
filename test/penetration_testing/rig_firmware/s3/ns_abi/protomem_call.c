#include "memoria_operor/memoria_operor.h"
static unsigned char buf[64] __attribute__((aligned(8)));
volatile unsigned long sink = 0;
void app_main(void);
void app_main(void)
{
    mem.put_u32(buf + 8, 0x11223344u);
    EMBED_CALL(memor.cpy, MemoriaCfg, .dst = buf + 16, .src = buf + 8, .bytes = 8);
    sink = mem.u32(buf + 16);
}
