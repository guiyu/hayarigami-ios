#include "../src/gfx/vertex.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
_Noreturn void hy_fail(const char *format, ...) { (void)format; abort(); }
int main(void) {
    struct { uint32_t before, attribute, after; } a = {0x12345678, 0, 0xabcdef01};
    hy_vertex_defaults(&a.attribute);
    hy_vertex_stream(&a.attribute, 7);
    hy_vertex_format(&a.attribute, 0x22, 128);
    int format; ptrdiff_t offset;
    hy_vertex_get_format(&a.attribute, &format, &offset);
    assert(format == 0x22 && offset == 128 && hy_vertex_get_stream(&a.attribute) == 7);
    assert(a.before == 0x12345678 && a.after == 0xabcdef01);
    hy_vertex_stream(&a.attribute, 2);
    hy_vertex_get_format(&a.attribute, &format, &offset);
    assert(format == 0x22 && offset == 128 && hy_vertex_get_stream(&a.attribute) == 2);
    HyVertexStream stream; hy_stream_defaults(&stream);
    hy_stream_stride(&stream, 32); hy_stream_divisor(&stream, 3);
    assert(hy_stream_get_stride(&stream) == 32 && hy_stream_get_divisor(&stream) == 3);
    puts("PASS: compact vertex state retains independent fields within NVN object sizes");
}
