#include "../src/gfx/gpu_time.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    uint64_t value=0;
    assert(hy_gpu_time_ns(100,10,112,15,12,&value) && value==104);
    assert(hy_gpu_time_ns(UINT64_MAX-100,10,UINT64_MAX,20,15,&value) && value==UINT64_MAX-50);
    assert(hy_gpu_time_ns(0,0,UINT64_MAX,UINT64_MAX,UINT64_MAX-1,&value) && value==UINT64_MAX-1);
    assert(!hy_gpu_time_ns(1,2,3,2,2,&value));assert(!hy_gpu_time_ns(3,1,2,4,2,&value));
    assert(!hy_gpu_time_ns(1,2,3,4,1,&value));assert(!hy_gpu_time_ns(1,2,3,4,5,&value));
    puts("PASS: GPU timestamp calibration, full-width multiply, invalid spans and out-of-window samples");
}
