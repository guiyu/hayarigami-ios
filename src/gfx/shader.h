#ifndef HY_SHADER_H
#define HY_SHADER_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
// Audited shipped SDK: +6f8/+6fc are payload lengths, +700/+704 padded extents,
// +708 prefix size, +714 the stage selector. SPH stages use a different enum.
typedef struct { uint32_t code_size,constant_size,constant_offset,extent,prefix,stage; } HyShaderLayout;
static inline int hy_shader_layout(const void *data,size_t available,HyShaderLayout *out) {
    if (!data || available<0x880) return 0;
    uint32_t words[0x880/4];memcpy(words,data,sizeof words);
    const uint32_t header[]={0x98761234,1,0x10,0x120,0xb,0x878,0,0,0x880};
    if (memcmp(words,header,sizeof header)) return 0;
    uint64_t code=words[0x6f8/4],constants=words[0x6fc/4],offset=words[0x700/4],total=words[0x704/4];
    uint32_t selector=words[0x714/4];
    if (words[0x708/4]!=0x30 || code<0x50 || (code&7) || selector>2 ||
        offset!=((0x30+code+255)&~(uint64_t)255) || total!=((offset+constants+255)&~(uint64_t)255)) return 0;
    const uint32_t stages[]={1,5,4};
    *out=(HyShaderLayout){(uint32_t)code,(uint32_t)constants,(uint32_t)offset,(uint32_t)total,0x30,stages[selector]};
    return 1;
}
#endif
