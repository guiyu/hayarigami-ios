// Full recorded draw execution using original SDK CPU states and original shader stages.
#include "../src/gfx/platform.m"
#include <assert.h>
#include <unistd.h>
static const char *shader_source,*shader_cache;
static void empty(void) {}
static HyShader *read_shader(NSString *key,unsigned stage) {
    HyShader *shader=[HyShader new];shader->stage=stage;NSString *source=@(shader_source);
    shader->control=[NSData dataWithContentsOfFile:[source stringByAppendingPathComponent:[key stringByAppendingString:@".control.bin"]]];
    shader->code=[NSData dataWithContentsOfFile:[source stringByAppendingPathComponent:[key stringByAppendingString:@".code.bin"]]];
    shader->constants=[NSData dataWithContentsOfFile:[source stringByAppendingPathComponent:[key stringByAppendingString:@".constants.bin"]]];
    assert(shader->control && shader->code && shader->constants);snprintf(shader->key,sizeof shader->key,"%s",key.UTF8String);return shader;
}
static void probe(void) {
 for(size_t i=0;i<hy_module_count;i++)if(hy_modules[i].dynamic[12])((void (*)(void))hy_range(&hy_modules[i],hy_modules[i].dynamic[12],4))();
 @autoreleasepool {
  initialize(NULL,0);uint64_t d=1;set_depth(&d,1);set_origin(&d,1);setenv("HY_SHADER_DIR",shader_cache,1);
  void *memory=NULL;assert(!posix_memalign(&memory,0x4000,0x40000));memset(memory,0,0x40000);
  PoolBuilder pb={.device=&d,.storage=memory,.size=0x40000,.flags=0x24};Pool *pool=NULL;assert(pool_initialize(&pool,&pb));
  TextureBuilder tb={.device=&d};texture_defaults(&tb);texture_size2d(&tb,16,16);texture_format(&tb,0x25);texture_flags(&tb,0x28);texture_storage(&tb,&pool,0);
  Texture image;assert(texture_initialize(&image,&tb));Texture *colors[]={&image};
  struct Vertex {float x,y,z;uint8_t color[4];};struct Vertex vertices[]={
    {-1,-1,0,{255,0,0,255}},{1,-1,0,{255,0,0,255}},{-1,1,0,{255,0,0,255}},{1,1,0,{255,0,0,255}}};
  memcpy((uint8_t *)memory+0x1000,vertices,sizeof vertices);uint16_t indices[]={0,1,2,3};memcpy((uint8_t *)memory+0x1800,indices,sizeof indices);
  float *matrix=(float *)((uint8_t *)memory+0x2000);for(unsigned i=0;i<4;i++)matrix[i*5]=matrix[16+i*5]=1;
  NSArray *programs=@[read_shader(@"8b48fccc06e7316ced2cffd9e0d2467a3526bec08b167bdf06cc98007b30878b",1),read_shader(@"803c26109c02466dc4077dcf0644fc0ce9b020061279fe179ce683b4a33eddb0",5)];
  void *program=(__bridge_retained void *)programs;QueueBuilder qb={.device=&d};void *q=NULL;assert(queue_initialize(&q,&qb));
  HyCommands c;command_initialize(&c,&d);command_memory(&c,&pool,0x10000,0x10000);hy_commands_begin(&c);
  command_targets(&c,1,colors,NULL,NULL,NULL);float blue[]={0,0,1,1};hy_commands_clear_color(&c,0,blue,15);
  const struct {const char *name;void (*bind)(HyCommands *,const void *);} states[]={
   {"nvnMultisampleState",command_multisample},{"nvnColorState",command_color},{"nvnBlendState",command_blend},
   {"nvnChannelMaskState",command_channels},{"nvnPolygonState",command_polygon},{"nvnDepthStencilState",command_depth_stencil}};
  for(unsigned i=0;i<6;i++) {
   char name[100];snprintf(name,sizeof name,"%sSetDefaults",states[i].name);_Alignas(8) uint8_t value[32]={0};((void (*)(void *))lookup(name))(value);
   if(i==0)((void (*)(void *,int))lookup("nvnMultisampleStateSetMultisampleEnable"))(value,0);
   if(i==1)((void (*)(void *,int))lookup("nvnColorStateSetAlphaTest"))(value,5);
   if(i==4)((void (*)(void *,int))lookup("nvnPolygonStateSetCullFace"))(value,0);
   if(i==5) {((void (*)(void *,int))lookup("nvnDepthStencilStateSetDepthTestEnable"))(value,0);((void (*)(void *,int))lookup("nvnDepthStencilStateSetStencilTestEnable"))(value,0);}
   states[i].bind(&c,value);
  }
  hy_commands_alpha_ref(&c,0);hy_commands_depth_range(&c,0,1);hy_commands_depth_clamp(&c,0);hy_commands_viewport(&c,0,0,16,16);hy_commands_scissor(&c,0,0,16,16);
  uint32_t attributes[]={0x22,0xc25};HyVertexStream streams[]={{16,0}};hy_commands_attributes(&c,2,attributes);hy_commands_streams(&c,1,streams);
  HyGpuBufferRange range={(uintptr_t)memory+0x1000,sizeof vertices};command_vertex_buffers(&c,0,1,&range);
  command_uniform(&c,0,0,(uintptr_t)memory+0x2000,128);command_program(&c,&program,3);command_draw_elements(&c,5,1,4,(uintptr_t)memory+0x1800);
  uint64_t handle=hy_commands_end(&c);program_finalize(&program);queue_submit(&q,1,&handle);
  uint8_t pixels[16*16*4];[(__bridge id<MTLTexture>)image.image getBytes:pixels bytesPerRow:64 fromRegion:MTLRegionMake2D(0,0,16,16) mipmapLevel:0];
  for(unsigned i=0;i<256;i++)assert(!memcmp(pixels+4*i,(uint8_t[]){255,0,0,255},4));
  texture_size2d(&tb,2,2);texture_storage(&tb,&pool,0x6000);Texture sampled;assert(texture_initialize(&sampled,&tb));
  uint8_t texels[]={255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255};
  [(__bridge id<MTLTexture>)sampled.image replaceRegion:MTLRegionMake2D(0,0,2,2) mipmapLevel:0 withBytes:texels bytesPerRow:8];
  DescriptorPool textures,samplers;assert(descriptor_initialize(&textures,&pool,0x3000,2));assert(descriptor_initialize(&samplers,&pool,0x4000,2));
  register_texture(&textures,1,&sampled,NULL);HySampler *sampling=[HySampler new];sampling->config=(HySamplerConfig){.wrap={7,7,7},.lod_max=1000,.anisotropy=1};
  void *sampling_object=(__bridge void *)sampling;register_sampler(&samplers,1,&sampling_object);
  programs=@[read_shader(@"67345bb9a9455678690221f25ae17ddf47bfe39b536c549ea262e98c6b464a06",1),read_shader(@"4bf8fbcc7ef00306824034754730d7300708e22c156cc1dd654c6e96c4e6a05f",5)];
  program=(__bridge_retained void *)programs;float positions[]={-1,-1,1,-1,-1,1,1,1};memcpy((uint8_t *)memory+0x1000,positions,sizeof positions);
  hy_commands_begin(&c);command_program(&c,&program,3);attributes[0]=0x16;streams[0].stride=8;
  hy_commands_attributes(&c,1,attributes);hy_commands_streams(&c,1,streams);range.size=sizeof positions;command_vertex_buffers(&c,0,1,&range);
  command_texture_pool(&c,&textures);command_sampler_pool(&c,&samplers);hy_commands_texture(&c,1,0,texture_handle(&d,1,1));
  for(unsigned i=0;i<4;i++)indices[i]=(uint16_t)(i+1);memcpy((uint8_t *)memory+0x1800,indices,sizeof indices);
  command_draw_elements_base(&c,5,1,4,(uintptr_t)memory+0x1800,-1);handle=hy_commands_end(&c);program_finalize(&program);queue_submit(&q,1,&handle);
  [(__bridge id<MTLTexture>)image.image getBytes:pixels bytesPerRow:64 fromRegion:MTLRegionMake2D(0,0,16,16) mipmapLevel:0];
  for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++)assert(!memcmp(pixels+4*(y*16+x),texels+4*((y/8)*2+x/8),4));
  descriptor_finalize(&textures);descriptor_finalize(&samplers);texture_finalize(&sampled);
  queue_finalize(&q);texture_finalize(&image);pool_finalize(&pool);free(memory);
  puts("PASS: original SDK states and shader pair executed through recorded queue, indexed color plus sampled UV quadrants correct in all 512 pixels, resources released");
 }
 exit(0);
}
int main(int argc,char **argv) {
 if(argc!=4)return 2;shader_source=argv[2];shader_cache=argv[3];hy_install_signals();alarm(60);hy_load(argv[1],0);hy_thread_init();hy_thread_main();
 void (*start)(uint64_t,uint64_t,void (*)(void),void (*)(void),void (*)(void))=hy_symbol("_ZN2nn4init5StartEmmPFvvES2_S2_");if(!start)hy_fail("SDK Start missing");start(0x100,0,empty,probe,empty);return 1;
}
