#ifndef HY_BC7_H
#define HY_BC7_H
#include <stdint.h>
#include <stddef.h>
int hy_bc7_decode(const void *blocks,size_t source_size,uint32_t width,uint32_t height,
                  void *pixels,size_t pitch,size_t destination_size);
#endif
