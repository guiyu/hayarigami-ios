#ifndef HY_SAMPLER_H
#define HY_SAMPLER_H
#import <Metal/Metal.h>
#include "../runtime/runtime.h"
#include <math.h>
typedef struct {
    int min_filter, mag_filter, wrap[3], compare_mode, compare_func, reduction;
    float lod_min, lod_max, bias, anisotropy, snap, border[4];
} HySamplerConfig;
static MTLSamplerAddressMode hy_sampler_wrap(int mode) {
    switch (mode) {
    case 1: return MTLSamplerAddressModeRepeat;
    case 3: return MTLSamplerAddressModeMirrorClampToEdge;
    case 5: return MTLSamplerAddressModeClampToBorderColor;
    case 6: return MTLSamplerAddressModeMirrorRepeat;
    case 7: return MTLSamplerAddressModeClampToEdge;
    default: hy_fail("unsupported sampler wrap mode: %d",mode);
    }
}
static id<MTLSamplerState> hy_sampler_create(id<MTLDevice> device, const HySamplerConfig *c) {
    if (c->min_filter<0 || c->min_filter>5 || c->mag_filter<0 || c->mag_filter>1 ||
        c->compare_mode<0 || c->compare_mode>1 || c->reduction || c->bias!=0 || c->snap!=0 ||
        !isfinite(c->anisotropy) || c->anisotropy<1 || c->anisotropy>16 || floorf(c->anisotropy)!=c->anisotropy ||
        !isfinite(c->lod_min) || !isfinite(c->lod_max) || c->lod_max<c->lod_min)
        hy_fail("unsupported sampler filter/LOD configuration");
    MTLSamplerDescriptor *d=[MTLSamplerDescriptor new];
    d.normalizedCoordinates=YES;
    d.minFilter=(c->min_filter&1) ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    d.magFilter=c->mag_filter ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    d.mipFilter=c->min_filter<2 ? MTLSamplerMipFilterNotMipmapped : c->min_filter<4 ? MTLSamplerMipFilterNearest : MTLSamplerMipFilterLinear;
    d.sAddressMode=hy_sampler_wrap(c->wrap[0]); d.tAddressMode=hy_sampler_wrap(c->wrap[1]); d.rAddressMode=hy_sampler_wrap(c->wrap[2]);
    d.lodMinClamp=fmaxf(0,c->lod_min); d.lodMaxClamp=fmaxf(0,c->lod_max);
    d.maxAnisotropy=(NSUInteger)c->anisotropy;
    if (c->compare_mode) {
        if (c->compare_func<1 || c->compare_func>8) hy_fail("invalid sampler comparison");
        d.compareFunction=(MTLCompareFunction)(c->compare_func-1);
    }
    if (c->wrap[0]==5 || c->wrap[1]==5 || c->wrap[2]==5) {
        if (!c->border[0] && !c->border[1] && !c->border[2] && !c->border[3]) d.borderColor=MTLSamplerBorderColorTransparentBlack;
        else if (!c->border[0] && !c->border[1] && !c->border[2] && c->border[3]==1) d.borderColor=MTLSamplerBorderColorOpaqueBlack;
        else if (c->border[0]==1 && c->border[1]==1 && c->border[2]==1 && c->border[3]==1) d.borderColor=MTLSamplerBorderColorOpaqueWhite;
        else hy_fail("custom sampler border color is not implemented");
    }
    return [device newSamplerStateWithDescriptor:d];
}
#endif
