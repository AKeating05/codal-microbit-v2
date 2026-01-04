#include "MicroBit.h"
#include "UserABI.h"

static MicroBit *_uBit;

static void abi_scroll(const char *s)
{
    _uBit->display.scroll(s);
}

static void abi_sleep(int ms)
{
    _uBit->sleep(ms);
}

static void abi_set_pixel(int x, int y, int value)
{
    _uBit->display.image.setPixelValue(x,y,value);
}

extern "C" 
const UserABI user_abi
__attribute__((section(".user_abi"), used)) =
{
    .scroll = abi_scroll,
    .sleep = abi_sleep,
    .set_pixel = abi_set_pixel
};

extern "C" __attribute__((noinline,used))
void init_user_abi(MicroBit &uBit)
{
    _uBit = &uBit;
}