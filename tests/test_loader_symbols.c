// Weak imports stay null; available guest/host symbols still resolve normally.
#include "../src/runtime/loader.c"
#include <assert.h>
#include <sys/wait.h>
#include <unistd.h>
void hy_register_gate(uint64_t a,const char *k,unsigned b,uint64_t c){(void)a;(void)k;(void)b;(void)c;}
static int host_value;
void *hy_service(const char *name){return !strcmp(name,"host") ? &host_value : NULL;}
int main(void) {
    uint8_t *image=calloc(1,0x10000),*exporter=calloc(1,0x10000);assert(image && exporter);
    const char names[]="\0optional\0provided\0host\0required\0";
    HySymbol imports[]={{0},{.name=1,.info=0x20},{.name=10,.info=0x20},{.name=19,.info=0x20},{.name=24,.info=0x10}};
    HySymbol exports[]={{0},{.name=10,.info=0x10,.section=1,.value=0x4500,.size=8}};
    hy_modules[0]=(HyModule){.name="fixture",.base=image,.size=0x10000,.text_end=0x100,.key_page=0x8000,.symbols=imports,.symbol_count=5,.strings=names};
    hy_modules[1]=(HyModule){.name="exporter",.base=exporter,.size=0x10000,.symbols=exports,.symbol_count=2,.strings=names};hy_module_count=2;
    HyRela *rel=(HyRela *)(image+0x4600);
    for(unsigned i=0;i<3;i++)rel[i]=(HyRela){.offset=0x4400+i*8,.info=((uint64_t)(i+1)<<32)|1025};
    assert(relocate(&hy_modules[0],0x4600,3*sizeof *rel)==3);
    uint64_t *values=(uint64_t *)(image+0x4400);assert(values[0]==0 && values[1]==(uintptr_t)(exporter+0x4500) && values[2]==(uintptr_t)&host_value);
    rel[0].info=((uint64_t)4<<32)|1025;
    pid_t child=fork();assert(child>=0);if(!child){relocate(&hy_modules[0],0x4600,sizeof *rel);_Exit(1);}
    int check_rc;assert(waitpid(child,&check_rc,0)==child && WIFEXITED(check_rc) && WEXITSTATUS(check_rc)==42);
    free(image);free(exporter);
    puts("PASS: absent weak import is null, guest and host definitions resolve, absent strong import stops");
}
