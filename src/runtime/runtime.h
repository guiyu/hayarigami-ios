#ifndef HY_RUNTIME_H
#define HY_RUNTIME_H
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
typedef struct { uint32_t name; uint8_t info, other; uint16_t section; uint64_t value, size; } HySymbol;
typedef struct { uint64_t offset, info; int64_t addend; } HyRela;
typedef struct {
    const char *name;
    uint8_t *base;
    size_t size, text_end, patch, key_page;
    uint64_t dynamic[32];
    HySymbol *symbols;
    size_t symbol_count;
    const char *strings;
} HyModule;
extern HyModule hy_modules[4];
extern size_t hy_module_count;
extern pthread_key_t hy_keys[8];
_Noreturn void hy_fail(const char *format, ...) __attribute__((format(printf, 1, 2)));
void *hy_range(HyModule *m, uint64_t offset, size_t length);
void *hy_symbol(const char *name);
void *hy_service(const char *name);
void *hy_audio_service(const char *name);
void *hy_graphics_service(const char *name);
// -1 means foreign event; otherwise handled. Operations: wait, poll, clear, destroy.
int hy_graphics_event(void *event, int operation);
void *hy_input_service(const char *name);
void hy_input_set(uint64_t buttons, int connected);
void hy_filesystem_init(const char *romfs);
void *hy_filesystem_service(const char *name);
void hy_load(const char *directory, int fixture);
void hy_thread_init(void);
void hy_gate(void);
void hy_register_gate(uint64_t address, const char *kind, unsigned arg, uint64_t original);
void hy_gate_dispatch(uint64_t address, uint64_t *registers);
void hy_install_signals(void);
int hy_thread_syscall(unsigned number, uint64_t *registers);
void hy_thread_main(void);
#endif
