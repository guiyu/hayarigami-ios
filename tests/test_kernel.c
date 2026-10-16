
/* Synthetic-only regression harness. Compile with xcrun clang -std=c11 -arch arm64 -O1. */
#include "../src/runtime/kernel.c"
#include <stdarg.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <assert.h>
HyModule hy_modules[4]; size_t hy_module_count; pthread_key_t hy_keys[8];
_Noreturn void hy_fail(const char *s,...) { va_list a;va_start(a,s);vfprintf(stderr,s,a);va_end(a);exit(42); }
int hy_thread_syscall(unsigned n,uint64_t *r) {(void)n;(void)r;return 0;}
void hy_gate(void) {}
static void inaccessible(uintptr_t p) {
 pid_t child=fork();assert(child>=0);if(!child){*(volatile uint64_t*)p=789;_Exit(0);}
 int status;assert(waitpid(child,&status,0)==child);
 assert(WIFSIGNALED(status)&&(WTERMSIG(status)==SIGBUS||WTERMSIG(status)==SIGSEGV));
}
static void *map_one(void *arg) {uint64_t r[31]={0};r[0]=(uintptr_t)address_space+0x10000000+(uintptr_t)arg*0x4000;r[1]=0x4000;syscall_impl(0x2c,r);assert(!r[0]);return NULL;}
int main(void) {
 struct rlimit limit={0,0};setrlimit(RLIMIT_CORE,&limit);
 uint64_t r[31]={0};r[1]=0x200000;syscall_impl(1,r);assert(!r[0]);uintptr_t heap=r[1],dest=(uintptr_t)address_space+0x30000000;
 *(uint64_t*)heap=123;
 r[0]=dest;r[1]=heap;r[2]=0x4000;syscall_impl(4,r);assert(!r[0]);assert(*(uint64_t*)dest==123);
 inaccessible(heap);
 struct {uint64_t base,size;uint32_t state,attr,perm,ipc,device,padding;} info;
 r[0]=(uintptr_t)&info;r[2]=heap;syscall_impl(6,r);assert(info.perm==0&&info.attr==1);
 r[0]=dest+0x4000;r[1]=heap;r[2]=0x4000;syscall_impl(4,r);assert(r[0]!=0);
 r[1]=0;syscall_impl(1,r);assert(r[0]!=0&&heap_size==0x200000);
 *(uint64_t*)dest=456;r[0]=dest;r[1]=heap;r[2]=0x4000;syscall_impl(5,r);assert(!r[0]);assert(*(uint64_t*)heap==456);inaccessible(dest);
 puts("PASS: source borrowing, shrink rejection, restored source, inaccessible unmapped destination");fflush(stdout);
 pthread_t workers[32];for(uintptr_t i=0;i<32;i++)assert(!pthread_create(&workers[i],NULL,map_one,(void*)i));for(int i=0;i<32;i++)assert(!pthread_join(workers[i],NULL));assert(physical_count==32);
 puts("PASS: 32 concurrent mappings retained");fflush(stdout);
 for(size_t i=0;i<physical_count;i++)assert(physical[i].source==0);
 r[1]=0;syscall_impl(1,r);assert(!r[0]);
 puts("PASS: physical mapping reused slot does not retain old source");
 return 0;
}
