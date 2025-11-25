#include <stdint.h>

__attribute__((section(".smallprog"))) void test()
{
    volatile int x = 0;
    while (1)
    {
        x++;
    }
}