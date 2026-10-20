#include "../src/runtime/input.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
_Noreturn void hy_fail(const char *format,...) {(void)format;abort();}
int main(void) {
    void (*touch_init)(void)=hy_input_service("_ZN2nn3hid21InitializeTouchScreenEv");
    assert(touch_init);touch_init();
    void (*read_touch)(void *)=hy_input_service("_ZN2nn3hid19GetTouchScreenStateILm16EEEvPNS0_16TouchScreenStateIXT_EEE");
    assert(read_touch);
    _Alignas(8) uint8_t touch[664]; memset(touch,0xa5,sizeof touch); read_touch(touch);
    int64_t first; memcpy(&first,touch,8); assert(first>0);
    for (size_t i=8;i<656;i++) assert(touch[i]==0);
    for (size_t i=656;i<sizeof touch;i++) assert(touch[i]==0xa5);
    read_touch(touch); int64_t second; memcpy(&second,touch,8); assert(second>first);
    uint32_t id=0;init();set_styles(1);set_ids(&id,1);hy_input_set(1,1);PadState state;read_pad(&state,&id);
    assert(state.buttons==1 && state.attributes==1);hy_input_set(0,1);read_pad(&state,&id);assert(state.buttons==0);
    puts("PASS: inactive touchscreen state, exact 656-byte ABI, and independent pad press/release");
}
