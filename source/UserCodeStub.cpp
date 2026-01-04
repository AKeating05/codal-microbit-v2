#include "MicroBit.h"

extern "C"
{
    extern uint8_t __user_start__;
}

typedef void (*user_entry)(void);

extern "C" __attribute__((noinline,used))
void user_stub(MicroBit &uBit)
{
    user_entry entry = (user_entry)&__user_start__;
    entry();
}
