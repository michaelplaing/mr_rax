// nextchild.c

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mr_rax/mr_rax.h"
#include "mr_rax/rax.h"
#include "mr_rax/rax_malloc.h"
#include "rax_internal.h"
#include "mr_rax_internal.h"

#define LIST_SIZE 256
#define UPPER_BOUND 100000

int nctest(void) {
    raxSetDebugMsg(0);
    rax* l1 = raxNew();
    rax* l2 = raxNew();
    uint8_t v[8];
    double startTime;
    double elapsed1;
    double elapsed2;

    startTime = (float)clock()/CLOCKS_PER_SEC;
    for (int i = 0; i < LIST_SIZE; i++) {
        uint64_t n = arc4random() % UPPER_BOUND;
        //for (int j = 0; j < 8; j++) v[j] = n >> ((7 - j) * 8) & 0xff;
        for (int j = 0; j < 8; j++) v[j] = i >> ((7 - j) * 8) & 0xff;
        raxInsert(l1, v, 8, NULL, NULL);
        raxInsert(l2, v, 8, NULL, NULL);
    }
    elapsed1 = (float)clock()/CLOCKS_PER_SEC - startTime;

    printf("init both done: elapsed: %f\n", elapsed1);

    raxIterator i1;
    raxStart(&i1, l1);
    raxIterator i2;
    raxStart(&i2, l2);

    raxSetDebugMsg(0);
    startTime = (float)clock()/CLOCKS_PER_SEC;
    raxSeek(&i1, "^", NULL, 0);
    raxSetDebugMsg(0);
    while(raxNext(&i1));
    elapsed1 = (float)clock()/CLOCKS_PER_SEC - startTime;
    printf("l1 done: elapsed: %f\n", elapsed1);

    raxSetDebugMsg(0);
    startTime = (float)clock()/CLOCKS_PER_SEC;
    // raxSeekSet(&i2);
    // while(raxNextInSet(&i2));
    raxSeek(&i2, "^", NULL, 0);
    while(raxNext(&i2));
    // for(uint64_t i = 0; raxNextInSet(&i2); i++) {
    //     for (int j = 0; j < 8; j++) v[j] = i >> ((7 - j) * 8) & 0xff;
    //     if (memcmp(i2.key, v, 8)) {
    //         printf("whoops: %llu\n", i);
    //         for (int k = 0; k < 8; k++) {
    //             printf("%02x ", v[k]);
    //             printf("%02x ", i2.key[k]);
    //         }
    //         puts("");
    //         break;
    //     }
    // }
    elapsed2 = (float)clock()/CLOCKS_PER_SEC - startTime;
    printf("l2 done: elapsed: %f\n", elapsed2);
    printf("elapsed1 / elapsed2: %f\n", elapsed1 / elapsed2);

    raxSetDebugMsg(0);

    //raxShowHex(l1);

    raxSetDebugMsg(0);
    raxFree(l1);
    raxFree(l2);
    return 0;
}

int main(int argc, char** argv) {
    return nctest();
}
