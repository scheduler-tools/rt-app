#include <stdio.h>

volatile int testVar = 0;

int test1() {

    testVar++;
    printf("first test %d\n", testVar);

    return 0;
} 

int test2() {

    printf("second test %d\n", testVar);

    return 0;
}
