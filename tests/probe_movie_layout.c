// Verify the original SDK's format-4 chroma packing without a video driver.
#include "../src/runtime/runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
static void empty(void){}
static void probe(void){
 for(size_t i=0;i<hy_module_count;i++)if(hy_modules[i].dynamic[12])((void (*)(void))hy_range(&hy_modules[i],hy_modules[i].dynamic[12],4))();
 typedef unsigned (*Convert)(void *,void *,int,int,const void *,const void *,const void *,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
 Convert convert=hy_symbol("_ZN5movie11DecoderImpl13ConvertToNv12EPhiiPKhS3_S3_mmmPim");if(!convert)hy_fail("original NV12 converter missing");
 uint8_t y[]={10,20,30,40,50,60,70,80},uv[]={90,110,100,120},out[16];memset(out,0xcd,sizeof out);int size=0;
 if(convert(NULL,out,4,2,y,uv,uv,4,4,4,(uintptr_t)&size,12) || size!=12 || memcmp(out,y,8) || memcmp(out+8,(uint8_t[]){90,110,100,120},4) || out[12]!=0xcd)hy_fail("original NV12 byte packing disagrees");
 puts("PASS: original movie SDK packs format-4 Y then interleaved UV with exact size and boundary");exit(0);
}
int main(int argc,char **argv){if(argc!=2)return 2;hy_install_signals();alarm(30);hy_load(argv[1],0);hy_thread_init();hy_thread_main();
 void (*start)(uint64_t,uint64_t,void (*)(void),void (*)(void),void (*)(void))=hy_symbol("_ZN2nn4init5StartEmmPFvvES2_S2_");if(!start)hy_fail("SDK Start missing");start(0x100,0,empty,probe,empty);return 1;}
