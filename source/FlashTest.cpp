__attribute__((section(".smallprog"))) const uint8_t small_program[4096] = 
{
    void test()
    {
        volatile int x = 0;
        while(1)
        {
            x++;
        }
    }
};
