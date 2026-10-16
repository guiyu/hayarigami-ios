#include "runtime.h"
#include <string.h>
#include <stdatomic.h>

typedef struct { int64_t sample; uint64_t buttons; int32_t lx, ly, rx, ry; uint32_t attributes, padding; } PadState;
_Static_assert(sizeof(PadState) == 40, "Horizon pad ABI");
static _Atomic uint64_t buttons, latched;
static _Atomic int connected;
static _Atomic int64_t sample;
static uint32_t styles, allowed_ids;
void hy_input_set(uint64_t value, int attached) {
    uint64_t old = atomic_exchange(&buttons, value);
    atomic_fetch_or(&latched, value & ~old);
    atomic_store(&connected, attached);
}
static void init(void) { styles = 0; allowed_ids = 0; }
static void set_styles(uint32_t value) { styles = value; }
static void set_ids(const uint32_t *ids, size_t count) {
    if (count > 10) hy_fail("invalid supported pad count");
    allowed_ids = 0;
    for (size_t i = 0; i < count; i++) if (ids[i] < 8) allowed_ids |= 1u << ids[i];
}
static uint32_t get_style(const uint32_t *id) {
    return *id == 0 && (allowed_ids & 1) && atomic_load(&connected) ? styles & 1 : 0;
}
static void read_pad(PadState *out, const uint32_t *id) {
    memset(out, 0, sizeof *out); out->sample = atomic_fetch_add(&sample, 1) + 1;
    if (get_style(id)) {
        out->attributes = 1;
        out->buttons = atomic_load(&buttons) | atomic_exchange(&latched, 0);
    }
}
static void hold_type(unsigned value) { if (value > 1) hy_fail("invalid Joy-Con hold type"); }
static int vibration_handles(void *out, int count, const uint32_t *id, uint32_t style) {
    (void)out; (void)count; (void)id; (void)style; return 0;
}
void *hy_input_service(const char *name) {
#define ENTRY(n, f) {n, (void *)f}
    static const struct { const char *name; void *fn; } table[] = {
        ENTRY("_ZN2nn3hid14InitializeNpadEv", init),
        ENTRY("_ZN2nn3hid22SetSupportedNpadIdTypeEPKjm", set_ids),
        ENTRY("_ZN2nn3hid24SetSupportedNpadStyleSetENS_4util10BitFlagSetILi32ENS0_12NpadStyleTagEEE", set_styles),
        ENTRY("_ZN2nn3hid15GetNpadStyleSetERKj", get_style),
        ENTRY("_ZN2nn3hid18SetNpadJoyHoldTypeENS0_15NpadJoyHoldTypeE", hold_type),
        ENTRY("_ZN2nn3hid12GetNpadStateEPNS0_16NpadFullKeyStateERKj", read_pad),
        ENTRY("_ZN2nn3hid12GetNpadStateEPNS0_17NpadHandheldStateERKj", read_pad),
        ENTRY("_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyDualStateERKj", read_pad),
        ENTRY("_ZN2nn3hid12GetNpadStateEPNS0_16NpadJoyLeftStateERKj", read_pad),
        ENTRY("_ZN2nn3hid12GetNpadStateEPNS0_17NpadJoyRightStateERKj", read_pad),
        ENTRY("_ZN2nn3hid25GetVibrationDeviceHandlesEPNS0_21VibrationDeviceHandleEiRKjNS_4util10BitFlagSetILi32ENS0_12NpadStyleTagEEE", vibration_handles),
    };
#undef ENTRY
    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
        if (!strcmp(name, table[i].name)) return table[i].fn;
    return NULL;
}
