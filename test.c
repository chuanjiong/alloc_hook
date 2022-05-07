#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// usage:
//  g++ alloc_hook.cpp -o ah.so -fPIC -shared -ldl -std=c++11 -rdynamic
//  g++ test.c -o t -rdynamic
//  LD_PRELOAD=./ah.so ./t

void m2()
{
    for (int i=0; i<3; i++) {
        char *p = (char *)malloc(100);
    }
}

void m1()
{
    m2();
}

void mf2()
{
    for (int i=0; i<3; i++) {
        char *p = (char *)malloc(100);
        free(p);
    }
}

void mf1()
{
    mf2();
}

int main(int argc, char **argv)
{
    m1();
    mf1();
    return 0;
}

