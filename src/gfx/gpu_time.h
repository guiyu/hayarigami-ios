#ifndef HY_GPU_TIME_H
#define HY_GPU_TIME_H
#include <stdint.h>

static inline int hy_gpu_time_ns(uint64_t cpu0,uint64_t gpu0,uint64_t cpu1,uint64_t gpu1,
                                 uint64_t sample,uint64_t *out) {
    if(!out || cpu1<cpu0 || gpu1<=gpu0 || sample<gpu0 || sample>gpu1) return 0;
    *out=cpu0+(uint64_t)(((__uint128_t)(sample-gpu0)*(cpu1-cpu0))/(gpu1-gpu0));
    return 1;
}
#endif
