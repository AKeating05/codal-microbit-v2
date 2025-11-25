#include "MicroBit.h"
MicroBit uBit;

__attribute__((section(".testpage")))
int main()
{
    uBit.init();
    uBit.display.scroll("FLASHED");
}