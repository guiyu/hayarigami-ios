// Shipped SDK graphics ABI and sparse-layout probes; no driver/device required.
#include "../src/runtime/runtime.h"
#include "../src/gfx/nvn_tex.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static void empty(void) {}
static void probe(void) {
    for (size_t i = 0; i < hy_module_count; i++) {
        HyModule *m = &hy_modules[i];
        if (m->dynamic[12]) ((void (*)(void))hy_range(m, m->dynamic[12], 4))();
    }
    void *(*get)(const char *) = hy_symbol("nvnBootstrapLoader");
    if (!get) hy_fail("NVN bootstrap missing");
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
        for (unsigned k=32;k<sizeof view;k++) if (view[k]!=0xa5) hy_fail("view overflow at %u",k);
    }
    // These internal CPU layout offsets are specific to the audited SDK SHA-256 in EVIDENCE.md.
    // No NVN device or graphics-driver state is fabricated for this probe.
    uint8_t *sdk_base=NULL;
    for (size_t j=0;j<hy_module_count;j++) if (!strcmp(hy_modules[j].name,"sdk")) sdk_base=hy_modules[j].base;
    if (!sdk_base || get("nvnTextureBuilderGetSparseTileLayout")!=sdk_base+0x462620)
        hy_fail("unexpected SDK layout entrypoint; re-audit before probing");
    const unsigned widths[]={1,19,128,127,256,64,128,129,1920,1920};
    const unsigned heights[]={1,16,127,128,64,256,128,129,1080,64};
    for (unsigned k=0;k<10;k++) {
        // Layout descriptor fields established from SDK+0x4444d0: flags,target,samples,levels,
        // width,height,depth,bytes/block,block width/height,dimensions,layered,sparse-width enable.
        _Alignas(8) uint32_t cpu_layout[16]={0x28,1,0,1,widths[k],heights[k],1,4,1,1,2,0,1};
        uint8_t tiles[8]={0};
        ((void (*)(void *,void *))(sdk_base+0x444578))(cpu_layout,tiles);
        uint64_t bytes=((uint64_t (*)(void *,void *))(sdk_base+0x4449ac))(cpu_layout,tiles);
        nvn_texdesc description={.target=1,.format=0x3d,.flags=0x28,.width=widths[k],.height=heights[k],.depth=1,.levels=1};
        nvn_texlayout actual; nvn_tex_layout(&description,&actual);
        if (actual.size!=bytes || actual.tileW!=tiles[0] || actual.tileH!=tiles[1] || actual.tileD!=tiles[2])
            hy_fail("SDK sparse-layout mismatch at %ux%u",widths[k],heights[k]);
    }
    _Alignas(8) uint8_t builder[128]={0}; uint32_t sparse[8]; memset(sparse,0xa5,sizeof sparse);
    ((void (*)(void *))get("nvnTextureBuilderSetDefaults"))(builder);
    ((void (*)(void *,int))get("nvnTextureBuilderSetFlags"))(builder,0x28);
    ((void (*)(void *,int))get("nvnTextureBuilderSetTarget"))(builder,1);
    ((void (*)(void *,int))get("nvnTextureBuilderSetFormat"))(builder,0x3d);
    ((void (*)(void *,int,int))get("nvnTextureBuilderSetSize2D"))(builder,1920,1080);
    ((void (*)(void *,void *))get("nvnTextureBuilderGetSparseTileLayout"))(builder,sparse);
    if (sparse[0]!=1 || sparse[1]!=128 || sparse[2]!=128 || sparse[3]!=1) hy_fail("SDK sparse tile shape mismatch");
    for (unsigned k=4;k<8;k++) if (sparse[k]!=0xa5a5a5a5) hy_fail("SDK sparse layout boundary");
    puts("PASS: SDK view boundary, sparse tile ABI and 10 independent CPU layout comparisons");
    puts("PASS: shipped SDK texture-view operations preserve bytes 32..63");
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
