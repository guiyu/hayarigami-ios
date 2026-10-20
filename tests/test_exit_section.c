#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../src/runtime/services.c"
_Noreturn void hy_fail(const char *format,...) {(void)format;exit(42);}
void *hy_graphics_service(const char *name){(void)name;return NULL;}
void *hy_movie_service(const char *name){(void)name;return NULL;}
void *hy_pcm_service(const char *name){(void)name;return NULL;}
void *hy_audio_service(const char *name){(void)name;return NULL;}
void *hy_account_service(const char *name){(void)name;return NULL;}
void *hy_input_service(const char *name){(void)name;return NULL;}
void *hy_filesystem_service(const char *name){(void)name;return NULL;}
int main(void) {
    void (*enter)(void)=hy_service("_ZN2nn2oe31EnterExitRequestHandlingSectionEv");
    void (*leave)(void)=hy_service("_ZN2nn2oe31LeaveExitRequestHandlingSectionEv");
    assert(enter && leave);enter();enter();leave();leave();
    pid_t child=fork();assert(child>=0);if(!child){leave();_exit(0);}
    int status;assert(waitpid(child,&status,0)==child);assert(WIFEXITED(status)&&WEXITSTATUS(status)==42);
    puts("PASS: nested native exit sections balance and reject unmatched leave");
}
