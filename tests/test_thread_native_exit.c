#include "../src/runtime/threads.c"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/mman.h>
void hy_thread_init(void) {}
void hy_gate_dispatch(uint64_t address,uint64_t *r){(void)address;(void)r;abort();}
_Noreturn void hy_fail(const char *format,...) {va_list args;va_start(args,format);vfprintf(stderr,format,args);va_end(args);abort();}
static pthread_key_t cleanup_key;
static _Atomic int cleaned;
static void cleanup(void *unused){
    (void)unused;char local;uintptr_t top=(uintptr_t)pthread_get_stackaddr_np(pthread_self());
    assert((uintptr_t)&local<top && (uintptr_t)&local>=top-pthread_get_stacksize_np(pthread_self()) && "pthread cleanup must run on the host stack");
    atomic_fetch_add(&cleaned,1);
}
static void guest(void *unused){(void)unused;assert(!pthread_setspecific(cleanup_key,(void *)1));hy_thread_returned();}
int main(void){
    hy_thread_main();assert(!pthread_key_create(&cleanup_key,cleanup));
    for(int i=0;i<100;i++){
        size_t size=65536;void *stack=mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(stack!=MAP_FAILED);
        uint64_t r[31]={0};r[1]=(uintptr_t)guest;r[3]=(uintptr_t)stack+size;r[4]=44;r[5]=0;assert(hy_thread_syscall(8,r));uint32_t handle=(uint32_t)r[1];
        r[0]=handle;assert(hy_thread_syscall(9,r));r[1]=(uintptr_t)&handle;r[2]=1;r[3]=UINT64_MAX;assert(hy_thread_syscall(0x18,r) && !r[0]);
        assert(!mprotect(stack,size,PROT_NONE));r[0]=handle;assert(hy_thread_syscall(0x16,r) && !r[0]);assert(!munmap(stack,size));
    }
    assert(atomic_load(&cleaned)==100);puts("PASS: 100 guest exits restore the host stack before signaling and pthread cleanup");
}
