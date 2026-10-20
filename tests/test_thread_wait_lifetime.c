#include "../src/runtime/threads.c"
#include <assert.h>
#include <stdio.h>
_Noreturn void hy_fail(const char *s,...) {(void)s;abort();}
void hy_thread_init(void) {}
_Noreturn void hy_thread_enter(uintptr_t a,uintptr_t b,uintptr_t c){(void)a;(void)b;(void)c;abort();}
static _Atomic int go;
static void *worker(void *arg) {pthread_setspecific(current_key,arg);while(!atomic_load(&go))sched_yield();hy_thread_returned();}
static void *waiter(void *arg) {
 uint64_t *result=arg;uint32_t handle=0x101;uint64_t r[31]={0};r[1]=(uintptr_t)&handle;r[2]=1;r[3]=UINT64_MAX;
 assert(hy_thread_syscall(0x18,r));result[0]=r[0];result[1]=r[1];return NULL;
}
int main(void) {
 hy_thread_main();Thread *old=calloc(1,sizeof *old),*replacement=calloc(1,sizeof *replacement);assert(old && replacement);
 pthread_cond_init(&old->changed,NULL);old->handle=0x101;old->started=1;threads[1]=old;
 assert(!pthread_create(&old->pthread,NULL,worker,old));uint64_t result[2]={UINT64_MAX,UINT64_MAX};pthread_t waiting;assert(!pthread_create(&waiting,NULL,waiter,result));
 for(;;) {pthread_mutex_lock(&lock);int registered=old->wait_references==1;pthread_mutex_unlock(&lock);if(registered)break;sched_yield();}
 // Model close withdrawing the public handle and a create reusing its slot.
 pthread_mutex_lock(&lock);threads[1]=replacement;replacement->handle=0x101;pthread_mutex_unlock(&lock);atomic_store(&go,1);
 assert(!pthread_join(waiting,NULL));assert(!result[0] && !result[1] && !replacement->exited);
 assert(!pthread_join(old->pthread,NULL));assert(!old->wait_references);pthread_cond_destroy(&old->changed);free(old);
 pthread_mutex_lock(&lock);threads[1]=NULL;pthread_mutex_unlock(&lock);free(replacement);
 puts("PASS: registered thread wait retains its target across public handle withdrawal and slot reuse");
}
