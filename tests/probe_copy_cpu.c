// Audit original SDK 2D format programming without issuing any GPU commands.
#include "../src/runtime/runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static void empty(void) {}
static void probe(void) {
 for(size_t i=0;i<hy_module_count;i++)if(hy_modules[i].dynamic[12])((void (*)(void))hy_range(&hy_modules[i],hy_modules[i].dynamic[12],4))();
 void *(*get)(const char *)=hy_symbol("nvnBootstrapLoader");uint8_t *sdk=NULL;
 for(size_t i=0;i<hy_module_count;i++)if(!strcmp(hy_modules[i].name,"sdk"))sdk=hy_modules[i].base;
 if(!sdk || get("nvnCommandBufferCopyTextureToTexture")!=sdk+0x4505d0)hy_fail("unexpected copy SDK");
 _Alignas(16) uint32_t words[512]={0},source[24]={0},destination[24]={0};
 source[3]=destination[3]=19;source[4]=destination[4]=17;
 _Alignas(16) uint8_t context[64]={0};uint32_t *begin=words,*end=words+512;memcpy(context,&begin,8);memcpy(context+8,&end,8);
 // uint64_t stack slots preserve Switch AAPCS layout across the Apple host ABI.
 typedef void (*Encode)(void *,void *,int,void *,int,int,int,int,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
 ((Encode)(sdk+0x450bf0))(context,source,0x3d,destination,0x25,8,8,0,0,19,17,32768,32768,15,1,0,1);
 uint32_t *finish;memcpy(&finish,context,8);if(finish<words || finish>words+512)hy_fail("copy encoder outside buffer");
 unsigned source_format=0,destination_format=0;
 for(uint32_t *p=words;p<finish;) {
  uint32_t header=*p++;unsigned mode=header>>29,count=(header>>16)&8191,method=(header&8191)*4;
  if(mode==4)continue;
  if(mode!=1)hy_fail("unexpected copy packet mode");
  if(count>(unsigned)(finish-p))hy_fail("short copy packet");
  for(unsigned i=0;i<count;i++) {
   unsigned target=method+4*i;
   if(target==0x230){source_format=p[i];printf("SOURCE_FORMAT %#x\n",p[i]);}
   if(target==0x200){destination_format=p[i];printf("DESTINATION_FORMAT %#x\n",p[i]);}
  }
  p+=count;
 }
 if(source_format!=0xd1 || destination_format!=0xd5)hy_fail("copy color formats not distinct");
 puts("PASS: original SDK programs distinct RGB10A2 and RGBA8 2D color formats for the first-copy path");exit(0);
}
int main(int argc,char **argv) {
 if(argc!=2)return 2;hy_install_signals();alarm(30);hy_load(argv[1],0);hy_thread_init();hy_thread_main();
 void (*start)(uint64_t,uint64_t,void (*)(void),void (*)(void),void (*)(void))=hy_symbol("_ZN2nn4init5StartEmmPFvvES2_S2_");
 if(!start)hy_fail("SDK Start missing");start(0x100,0,empty,probe,empty);return 1;
}
