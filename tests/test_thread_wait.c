#include "../src/runtime/threads.c"
#include <assert.h>
#include <stdio.h>
_Noreturn void hy_fail(const char *s,...) {(void)s;abort();}
void hy_thread_init(void) {}
_Noreturn void hy_thread_enter(uintptr_t a,uintptr_t b,uintptr_t c){(void)a;(void)b;(void)c;abort();}
static _Atomic int go;
static void *worker(void *arg) {pthread_setspecific(current_key,arg);while(!atomic_load(&go))sched_yield();hy_thread_returned();}
static void *release_worker(void *arg) {(void)arg;struct timespec pause={0,5000000};nanosleep(&pause,NULL);atomic_store(&go,1);return NULL;}
static void *cancel_main(void *arg){(void)arg;struct timespec p={0,2000000};nanosleep(&p,NULL);uint64_t r[31]={0x100};assert(hy_thread_syscall(0x19,r)&&!r[0]);return NULL;}
int main(void) {
 hy_thread_main();Thread *t=calloc(1,sizeof *t);assert(t);pthread_cond_init(&t->changed,NULL);t->handle=0x101;t->started=1;threads[1]=t;assert(!pthread_create(&t->pthread,NULL,worker,t));
 uint32_t handles[]={0x100,0x101};uint64_t r[31]={0};r[1]=(uintptr_t)handles;r[2]=2;r[3]=0;assert(hy_thread_syscall(0x18,r) && r[0]==0xea01);
 r[1]=(uintptr_t)handles;r[2]=2;r[3]=1000000;assert(hy_thread_syscall(0x18,r) && r[0]==0xea01);
 pthread_t release;assert(!pthread_create(&release,NULL,release_worker,NULL));r[1]=(uintptr_t)handles;r[2]=2;r[3]=UINT64_MAX;
 assert(hy_thread_syscall(0x18,r) && !r[0] && r[1]==1);assert(!pthread_join(release,NULL));
 r[0]=0x101;assert(hy_thread_syscall(0x16,r) && !r[0] && !threads[1]);
 r[1]=(uintptr_t)handles;r[2]=2;r[3]=0;assert(hy_thread_syscall(0x18,r) && r[0]==0xe401);
 pthread_t cancel;assert(!pthread_create(&cancel,NULL,cancel_main,NULL));memset(r,0,sizeof r);r[3]=UINT64_MAX;assert(hy_thread_syscall(0x18,r)&&r[0]==0xec01);assert(!pthread_join(cancel,NULL));
 r[3]=1000000;assert(hy_thread_syscall(0x18,r)&&r[0]==0xea01);
 puts("PASS: real thread-exit wait, multi-handle result index, zero/finite timeout and joined handle close");
}
