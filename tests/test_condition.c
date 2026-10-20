// Horizon condition-variable semantics: release, signal, lock handoff and timeout.
#include "../src/runtime/threads.c"
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
_Noreturn void hy_fail(const char *s,...) {(void)s;abort();}
void hy_thread_init(void) {}
_Noreturn void hy_thread_enter(uintptr_t a,uintptr_t b,uintptr_t c){(void)a;(void)b;(void)c;abort();}
static _Atomic uint32_t mutex_word, condition_word;
static _Atomic int completed;
static Thread worker;
static void *wait_condition(void *unused) {
    (void)unused; pthread_setspecific(current_key,&worker);
    uint64_t r[31]={(uintptr_t)&mutex_word,(uintptr_t)&condition_word,0x101,UINT64_MAX};
    assert(hy_thread_syscall(0x1c,r) && !r[0]);
    assert(atomic_load(&mutex_word)==0x101); atomic_store(&completed,1);
    r[0]=(uintptr_t)&mutex_word; assert(hy_thread_syscall(0x1b,r)); return NULL;
}
int main(void) {
    hy_thread_main(); pthread_cond_init(&worker.changed,NULL); worker.handle=0x101; worker.priority=44; threads[1]=&worker;
    atomic_store(&mutex_word,0x100);
    uint64_t r[31]={(uintptr_t)&mutex_word,(uintptr_t)&condition_word,0x100,0};
    assert(hy_thread_syscall(0x1c,r)); assert(r[0]==0xea01 && !atomic_load(&mutex_word) && atomic_load(&condition_word)==1);
    r[0]=(uintptr_t)&condition_word;r[1]=-1;assert(hy_thread_syscall(0x1d,r) && !r[0]);assert(!atomic_load(&condition_word));
    atomic_store(&mutex_word,0x101);pthread_t thread;assert(!pthread_create(&thread,NULL,wait_condition,NULL));
    for (;;) { pthread_mutex_lock(&lock);int waiting=worker.waiting;pthread_mutex_unlock(&lock);if(waiting==3)break;usleep(1000); }
    assert(!atomic_load(&mutex_word) && atomic_load(&condition_word)==1);
    uint32_t expected=0;assert(atomic_compare_exchange_strong(&mutex_word,&expected,0x100));
    r[0]=(uintptr_t)&condition_word;r[1]=1;assert(hy_thread_syscall(0x1d,r) && !r[0]);
    assert(atomic_load(&mutex_word)==(0x100|0x40000000) && !atomic_load(&condition_word) && !atomic_load(&completed));
    r[0]=(uintptr_t)&mutex_word;assert(hy_thread_syscall(0x1b,r) && !r[0]);
    assert(!pthread_join(thread,NULL));assert(atomic_load(&completed) && !atomic_load(&mutex_word));
    atomic_store(&mutex_word,0x100);
    r[0]=(uintptr_t)&mutex_word;r[1]=(uintptr_t)&condition_word;r[2]=0x100;r[3]=1000000;
    assert(hy_thread_syscall(0x1c,r) && r[0]==0xea01 && !atomic_load(&mutex_word));
    puts("PASS: condition waits release mutex, signals wait for ownership, and zero/finite timeouts release without ownership");
}
