extern "C" void testpage(void) __attribute__((section(".testpage"), used));

extern "C" void testpage(void)
{
    volatile int x = 0;
    while(1)
    {
        x++;
    }
}