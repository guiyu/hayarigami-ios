#ifndef HY_VERTEX_H
#define HY_VERTEX_H
#include "../runtime/runtime.h"
// Host-private encoding fits the NVN ABI's 4-byte attribute and 8-byte stream.
// Every field is read back through this adapter, including command recording.
typedef struct { uint32_t stride, divisor; } HyVertexStream;
static inline void hy_vertex_defaults(uint32_t *a) { *a = 0; }
static inline void hy_vertex_format(uint32_t *a, int format, ptrdiff_t offset) {
    if (format < 0 || format > 255 || offset < 0 || offset >= (1 << 19))
        hy_fail("invalid NVN vertex format/offset");
    *a = (*a & 0xf8000000u) | (uint32_t)format | ((uint32_t)offset << 8);
}
static inline void hy_vertex_stream(uint32_t *a, int stream) {
    if (stream < 0 || stream >= 32) hy_fail("invalid NVN vertex stream index");
    *a = (*a & 0x07ffffffu) | ((uint32_t)stream << 27);
}
static inline void hy_vertex_get_format(const uint32_t *a, int *format, ptrdiff_t *offset) {
    *format = *a & 255; *offset = (*a >> 8) & 0x7ffff;
}
static inline int hy_vertex_get_stream(const uint32_t *a) { return *a >> 27; }
static inline void hy_stream_defaults(HyVertexStream *s) { s->stride = s->divisor = 0; }
static inline void hy_stream_stride(HyVertexStream *s, ptrdiff_t stride) {
    if (stride < 0 || (uint64_t)stride > UINT32_MAX) hy_fail("invalid NVN vertex stride");
    s->stride = (uint32_t)stride;
}
static inline void hy_stream_divisor(HyVertexStream *s, int divisor) {
    if (divisor < 0) hy_fail("invalid NVN vertex divisor");
    s->divisor = (uint32_t)divisor;
}
static inline ptrdiff_t hy_stream_get_stride(const HyVertexStream *s) { return s->stride; }
static inline int hy_stream_get_divisor(const HyVertexStream *s) { return (int)s->divisor; }
#endif
