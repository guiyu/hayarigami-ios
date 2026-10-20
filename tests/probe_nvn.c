// Shipped SDK graphics ABI and sparse-layout probes; no driver/device required.
#include "../src/runtime/runtime.h"
#include "../src/gfx/nvn_tex.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
extern const char *const hy_nvn_names[];
extern const size_t hy_nvn_name_count;
static void empty(void) {}
static void probe(void) {
    for (size_t i = 0; i < hy_module_count; i++) {
        HyModule *m = &hy_modules[i];
        if (m->dynamic[12]) ((void (*)(void))hy_range(m, m->dynamic[12], 4))();
    }
    void *(*get)(const char *) = hy_symbol("nvnBootstrapLoader");
    if (!get) hy_fail("NVN bootstrap missing");
    for (size_t k=0;k<hy_nvn_name_count;k++) for (size_t j=0;j<hy_module_count;j++) {
        uint8_t *fn=get(hy_nvn_names[k]); HyModule *m=&hy_modules[j];
        if (fn>=m->base && fn<m->base+m->size)
            printf("ENTRY %s %s+%#lx\n",hy_nvn_names[k],m->name,(unsigned long)(fn-m->base));
    }
    _Alignas(8) uint8_t view[64]; memset(view,0xa5,sizeof view);
    const char *names[]={"nvnTextureViewSetDefaults","nvnTextureViewSetLevels","nvnTextureViewSetLayers","nvnTextureViewSetFormat","nvnTextureViewSetSwizzle","nvnTextureViewSetDepthStencilMode","nvnTextureViewSetTarget"};
    for (unsigned i=0;i<sizeof names/sizeof names[0];i++) {
        void *fn=get(names[i]);
        if (!fn) hy_fail("missing %s",names[i]);
        for (size_t j=0;j<hy_module_count;j++) if ((uint8_t *)fn>=hy_modules[j].base && (uint8_t *)fn<hy_modules[j].base+hy_modules[j].size)
            printf("%s %s+%#lx\n",names[i],hy_modules[j].name,(unsigned long)((uint8_t *)fn-hy_modules[j].base));
        if (i==0) ((void (*)(void *))fn)(view);
        else if (i==1 || i==2) ((void (*)(void *,int,int))fn)(view,0,1);
        else if (i==4) ((void (*)(void *,int,int,int,int))fn)(view,2,3,4,5);
        else ((void (*)(void *,int))fn)(view,i==3?0x25:i==6?1:0);
        for (unsigned k=16;k<sizeof view;k++) if (view[k]!=0xa5) hy_fail("view overflow at %u",k);
    }
    const struct { const char *name; size_t size; } states[]={
        {"nvnMultisampleState",24},{"nvnColorState",4},{"nvnBlendState",8},
        {"nvnChannelMaskState",4},{"nvnPolygonState",4},{"nvnDepthStencilState",8},{"nvnSamplerBuilder",96}};
    void *(*host_get)(const char *)=hy_graphics_service("nvnBootstrapLoader");
    for (unsigned k=0;k<sizeof states/sizeof states[0];k++) {
        char name[80]; snprintf(name,sizeof name,"%sSetDefaults",states[k].name);
        _Alignas(8) uint8_t state[128]; memset(state,0xa5,sizeof state);
        if (host_get(name)!=get(name)) hy_fail("pure SDK state was not preserved: %s",name);
        ((void (*)(void *))host_get(name))(state);
        for (size_t j=states[k].size;j<sizeof state;j++) if (state[j]!=0xa5) hy_fail("state ABI boundary: %s",name);
    }
    for(unsigned query=0x11;query<=0x12;query++) {
        int original_reserved=-1,host_reserved=-1;
        ((void (*)(const void *,unsigned,int *))get("nvnDeviceGetInteger"))(NULL,query,&original_reserved);
        ((void (*)(const void *,unsigned,int *))host_get("nvnDeviceGetInteger"))(NULL,query,&host_reserved);
        printf("RESERVED query=%#x original=%d host=%d\n",query,original_reserved,host_reserved);
        if(original_reserved!=256 || host_reserved!=original_reserved)hy_fail("reserved descriptor range differs from title SDK");
    }
    _Alignas(8) uint8_t sample_state[32]; memset(sample_state,0xa5,sizeof sample_state);
    ((void (*)(void *))host_get("nvnMultisampleStateSetDefaults"))(sample_state);
    ((void (*)(void *,int))host_get("nvnMultisampleStateSetSamples"))(sample_state,4);
    ((void (*)(void *,int))host_get("nvnMultisampleStateSetMultisampleEnable"))(sample_state,1);
    if (((int (*)(const void *))host_get("nvnMultisampleStateGetSamples"))(sample_state)!=4 ||
        !((int (*)(const void *))host_get("nvnMultisampleStateGetMultisampleEnable"))(sample_state)) hy_fail("SDK multisample state round trip");
    for (unsigned j=24;j<32;j++) if (sample_state[j]!=0xa5) hy_fail("multisample state boundary");
    _Alignas(8) uint8_t sampler_builder[112]; memset(sampler_builder,0xa5,sizeof sampler_builder);
    ((void (*)(void *))host_get("nvnSamplerBuilderSetDefaults"))(sampler_builder);
    ((void (*)(void *,int,int))host_get("nvnSamplerBuilderSetMinMagFilter"))(sampler_builder,5,1);
    ((void (*)(void *,int,int,int))host_get("nvnSamplerBuilderSetWrapMode"))(sampler_builder,7,1,6);
    int min_filter=0,mag_filter=0,wrap[3]={0};
    ((void (*)(const void *,int *,int *))host_get("nvnSamplerBuilderGetMinMagFilter"))(sampler_builder,&min_filter,&mag_filter);
    ((void (*)(const void *,int *,int *,int *))host_get("nvnSamplerBuilderGetWrapMode"))(sampler_builder,&wrap[0],&wrap[1],&wrap[2]);
    if (min_filter!=5 || mag_filter!=1 || memcmp(wrap,(int[]){7,1,6},sizeof wrap)) hy_fail("SDK sampler state round trip");
    for (unsigned j=96;j<sizeof sampler_builder;j++) if (sampler_builder[j]!=0xa5) hy_fail("sampler-builder boundary");
    // These internal CPU layout offsets are specific to the audited SDK SHA-256 in EVIDENCE.md.
    // No NVN device or graphics-driver state is fabricated for this probe.
    uint8_t *sdk_base=NULL;
    for (size_t j=0;j<hy_module_count;j++) if (!strcmp(hy_modules[j].name,"sdk")) sdk_base=hy_modules[j].base;
    if (!sdk_base || get("nvnTextureBuilderGetSparseTileLayout")!=sdk_base+0x462620)
        hy_fail("unexpected SDK layout entrypoint; re-audit before probing");
    const unsigned widths[]={1,19,128,127,256,64,128,129,1920,1920,255,256,257,256};
    const unsigned heights[]={1,16,127,128,64,256,128,129,1080,64,255,256,257,255};
    unsigned mismatches=0;
    for (unsigned bpp=1;bpp<=16;bpp*=2) for (unsigned k=0;k<14;k++) {
        // Layout descriptor fields established from SDK+0x4444d0: flags,target,samples,levels,
        // width,height,depth,bytes/block,block width/height,dimensions,layered,sparse-width enable.
        _Alignas(8) uint32_t cpu_layout[16]={0x28,1,0,1,widths[k],heights[k],1,bpp,bpp==16 ? 4 : 1,bpp==16 ? 4 : 1,2,0,1};
        uint8_t tiles[8]={0};
        ((void (*)(void *,void *))(sdk_base+0x444578))(cpu_layout,tiles);
        uint64_t bytes=((uint64_t (*)(void *,void *))(sdk_base+0x4449ac))(cpu_layout,tiles);
        nvn_texdesc description={.target=1,.format=bpp==1 ? 1 : bpp==2 ? 5 : bpp==4 ? 0x3d : bpp==8 ? 0x36 : 0x4d,.flags=0x28,.width=widths[k],.height=heights[k],.depth=1,.levels=1};
        uint32_t public_layout[2][8];
        for (unsigned api=0;api<2;api++) {
            void *(*query)(const char *)=api ? host_get : get;
            _Alignas(8) uint8_t candidate[128]={0};memset(public_layout[api],0xa5,sizeof public_layout[api]);
            ((void (*)(void *))query("nvnTextureBuilderSetDefaults"))(candidate);
            ((void (*)(void *,int))query("nvnTextureBuilderSetFlags"))(candidate,0x28);
            ((void (*)(void *,int))query("nvnTextureBuilderSetFormat"))(candidate,description.format);
            ((void (*)(void *,int,int))query("nvnTextureBuilderSetSize2D"))(candidate,widths[k],heights[k]);
            ((void (*)(void *,void *))query("nvnTextureBuilderGetSparseTileLayout"))(candidate,public_layout[api]);
        }
        if (memcmp(public_layout[0],public_layout[1],sizeof public_layout[0]))
            hy_fail("SDK public sparse-layout mismatch at %ux%u bpp%u",widths[k],heights[k],bpp);
        nvn_texlayout actual; nvn_tex_layout(&description,&actual);
        printf("LAYOUT bpp=%u w=%u h=%u bytes=%llu tile=%u,%u,%u\n",bpp,widths[k],heights[k],(unsigned long long)bytes,tiles[0],tiles[1],tiles[2]);
        if (actual.size!=bytes || actual.tileW!=tiles[0] || actual.tileH!=tiles[1] || actual.tileD!=tiles[2])
            mismatches++;
    }
    if (mismatches) hy_fail("SDK sparse-layout mismatches: %u",mismatches);
    _Alignas(8) uint8_t builder[128]={0}; uint32_t sparse[8]; memset(sparse,0xa5,sizeof sparse);
    ((void (*)(void *))get("nvnTextureBuilderSetDefaults"))(builder);
    ((void (*)(void *,int))get("nvnTextureBuilderSetFlags"))(builder,0x28);
    ((void (*)(void *,int))get("nvnTextureBuilderSetTarget"))(builder,1);
    ((void (*)(void *,int))get("nvnTextureBuilderSetFormat"))(builder,0x3d);
    ((void (*)(void *,int,int))get("nvnTextureBuilderSetSize2D"))(builder,1920,1080);
    ((void (*)(void *,void *))get("nvnTextureBuilderGetSparseTileLayout"))(builder,sparse);
    if (sparse[0]!=1 || sparse[1]!=128 || sparse[2]!=128 || sparse[3]!=1) hy_fail("SDK sparse tile shape mismatch");
    for (unsigned k=4;k<8;k++) if (sparse[k]!=0xa5a5a5a5) hy_fail("SDK sparse layout boundary");
    const char *packaged_names[]={"nvnTextureBuilderSetPackagedTextureLayout","nvnTextureBuilderGetPackagedTextureLayout"};
    for (unsigned i=0;i<2;i++) printf("PACKAGED %s sdk+%#lx\n",packaged_names[i],(unsigned long)((uint8_t *)get(packaged_names[i])-sdk_base));
    const uint8_t packaged_bytes[8]={3,4,0,0,7,0,1,0};
    ((void (*)(void *,const void *))get(packaged_names[0]))(builder,packaged_bytes);
    printf("SDK packaged builder[54..57]: %02x %02x %02x %02x\n",builder[0x54],builder[0x55],builder[0x56],builder[0x57]);
    for (unsigned api=0;api<2;api++) {
        void *(*query)(const char *)=api ? host_get : get;
        _Alignas(8) uint8_t candidate[128]={0};uint32_t returned[2]={0xa5a5a5a5,0xa5a5a5a5};
        ((void (*)(void *))query("nvnTextureBuilderSetDefaults"))(candidate);
        ((void (*)(void *,const void *))query(packaged_names[0]))(candidate,packaged_bytes);
        if (!((uint32_t (*)(const void *,void *))query(packaged_names[1]))(candidate,returned) ||
            returned[0]!=0x403 || returned[1]!=0xa5a5a5a5) hy_fail("packaged-layout getter ABI");
    }
    const uint32_t packaged_cases[][6]={{1920,1080,16,4,3,4},{257,257,16,4,3,4},
        {255,255,16,4,3,4},{19,17,16,4,2,0},{19,16,4,1,0,0},{19,16,4,1,1,0}};
    for (unsigned i=0;i<sizeof packaged_cases/sizeof packaged_cases[0];i++) {
        const uint32_t *v=packaged_cases[i];
        uint32_t descriptor[16]={0x20,1,0,1,v[0],v[1],1,v[2],v[3],v[3],2,0,1};
        uint8_t tiles[3]={(uint8_t)v[5],(uint8_t)v[4],0};
        uint64_t bytes=((uint64_t (*)(void *,void *))(sdk_base+0x4449ac))(descriptor,tiles);
        nvn_texdesc d={.target=1,.format=v[2]==16 ? 0x4d : 0x25,.flags=0x20,.width=v[0],.height=v[1],.depth=1,.levels=1,
            .pkg_block_h=v[4]+1,.pkg_block_w=v[5]+1};
        nvn_texlayout actual;nvn_tex_layout(&d,&actual);
        if (bytes!=actual.size || actual.tileH!=v[4] || actual.tileW!=v[5])
            hy_fail("explicit packaged-layout mismatch case %u: SDK %llu host %llu",i,(unsigned long long)bytes,(unsigned long long)actual.size);
    }
    puts("PASS: SDK render-state preservation, view boundary, sparse tile ABI and 70 automatic and 6 packaged CPU layout comparisons");
    exit(0);
}
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    hy_install_signals(); alarm(30); hy_load(argv[1], 0); hy_thread_init(); hy_thread_main();
    void (*start)(uint64_t, uint64_t, void (*)(void), void (*)(void), void (*)(void)) =
        hy_symbol("_ZN2nn4init5StartEmmPFvvES2_S2_");
    if (!start) hy_fail("SDK Start missing");
    start(0x100, 0, empty, probe, empty); return 1;
}
