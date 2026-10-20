#include "../src/media/decoder.m"
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
_Noreturn void hy_fail(const char *format,...) {va_list args;va_start(args,format);vfprintf(stderr,format,args);va_end(args);abort();}
void *hy_symbol(const char *name){hy_fail("unexpected movie export %s",name);}
int main(void){@autoreleasepool{
    HyMovieDecoder *s=[HyMovieDecoder new];s->width=1920;s->height=1080;s->format=4;
    CVPixelBufferRef image=NULL;NSDictionary *attributes=@{(id)kCVPixelBufferIOSurfacePropertiesKey:@{}};
    assert(!CVPixelBufferCreate(NULL,1920,1080,kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange,(__bridge CFDictionaryRef)attributes,&image));
    for(int i=0;i<120;i++){
        decoded_video((__bridge void *)s,NULL,0,0,image,CMTimeMake(i,30),kCMTimeInvalid);
        __weak NSData *bytes=s->pending.lastObject.bytes;
        assert(bytes.length==1920*1080*3/2);
        [s->pending removeAllObjects];[s->ready removeAllObjects];
        assert(!bytes && "decoded frame remains autoreleased after the callback returns");
    }
    CFRelease(image);puts("PASS: 120 full HD video callbacks release frame storage immediately after consumption");
}}
