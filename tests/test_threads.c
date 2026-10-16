
/* Compile with xcrun clang -std=c11 -arch arm64 -O0 -g -fsanitize=thread. */
#include "../src/runtime/threads.c"
#include <stdio.h>
#include <assert.h>
_Noreturn void hy_fail(const char *s,...) {(void)s;exit(42);}
void hy_thread_init(void) {}
_Noreturn void hy_thread_enter(uintptr_t a,uintptr_t b,uintptr_t c){(void)a;(void)b;(void)c;exit(42);}
static _Atomic int start;
static void *writer(void *arg){(void)arg;while(!atomic_load(&start)){}for(int i=0;i<100000;i++){uint64_t r[31]={0};r[0]=0x100;r[1]=i%64;hy_thread_syscall(0x0d,r);}return NULL;}
static void *reader(void *arg){(void)arg;while(!atomic_load(&start)){}volatile uint64_t sum=0;for(int i=0;i<100000;i++){uint64_t r[31]={0};r[1]=0x100;hy_thread_syscall(0x0c,r);sum+=r[1];}return NULL;}
int main(void){hy_thread_main();pthread_t a,b;assert(!pthread_create(&a,NULL,writer,NULL));assert(!pthread_create(&b,NULL,reader,NULL));atomic_store(&start,1);assert(!pthread_join(a,NULL));assert(!pthread_join(b,NULL));puts("PASS: concurrent priority read/write (run with TSan)");return 0;}
