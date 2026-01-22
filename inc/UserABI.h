#pragma once
#include <stdint.h>
#define USER_ABI_ADDR 0x75F00

typedef struct UserABI
{
    // void (*scroll)(const char *);
    void (*sleep)(int);
    void (*set_pixel)(int x, int y, int value);
} UserABI;


static inline const UserABI *user_abi(void)
{
    return ((const UserABI *)USER_ABI_ADDR);
}

static inline void abi_sleep(int ms)
{
    user_abi->sleep(ms);
}

static inline void abi_set_pixel(int x, int y, int value)
{
    user_abi->set_pixel(int x, int y, int value);
}