#include "mmgr/protomem.h"
static unsigned char buf[64] __attribute__((aligned(8)));
volatile unsigned long sink = 0;
void app_main(void);
void app_main(void)
{
    mem.put_u32(buf + 8, 0x11223344u);
    mem.cpy(buf + 16, buf + 8, 8);
    sink = mem.u32(buf + 16);
}
