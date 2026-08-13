// Drives both hot loops so neither is dead-code eliminated, and prints a checksum so the
// results are comparable across builds.
#include "opq.h"
#include "pub.h"
#include <stdio.h>

size_t hot_opq(uint8_t slot, int n);
size_t hot_pub(uint8_t slot, int n);

int main(int argc, char **argv)
{
    uint8_t src[256];
    for (int i = 0; i < 256; i++)
    {
        src[i] = (uint8_t)(i ^ argc);
    }
    rt_produce(0, src, sizeof(src));
    pub_produce(0, src, sizeof(src));

    size_t a = hot_opq(0, 256);
    size_t b = hot_pub(0, 256);
    printf("opq=%zu pub=%zu argv=%p\n", a, b, (void *)argv);
    return (a == b) ? 0 : 1;
}
