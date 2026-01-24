#pragma once
#include <stdint.h>

struct UserABI
{
    void (*scroll)(const char *);
    int (*random)(int);
    void (*sleep)(int);
    void (*set_pixel)(int x, int y, int value);
};