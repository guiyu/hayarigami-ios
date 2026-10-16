// Signed page remapping follows the generic D2R Mobile loader design.
// Copyright (c) 2026 jhaizhou; MIT license: third_party/D2R-Mobile-LICENSE.
#include "runtime.h"
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>

static const struct mach_header_64 *guest_image(void) {
    static const struct mach_header_64 *header;
    if (header) return header;
    char path[4096]; uint32_t capacity = sizeof path;
    if (_NSGetExecutablePath(path, &capacity)) hy_fail("executable path too long");
    char *last = strrchr(path, '/');
    const char *suffix = "/Frameworks/Guest.framework/Guest";
    if (!last || (size_t)(last - path) + strlen(suffix) + 1 > sizeof path) hy_fail("framework path too long");
    strcpy(last, suffix);
    if (!dlopen(path, RTLD_NOW | RTLD_LOCAL)) hy_fail("signed Guest framework load failed: %s", dlerror());
    for (uint32_t i = 0; i < _dyld_image_count(); i++)
        if (!strcmp(_dyld_get_image_name(i), path)) header = (const struct mach_header_64 *)_dyld_get_image_header(i);
    if (!header) hy_fail("loaded Guest image header not found");
    return header;
}
static void remap(uintptr_t destination, const void *source, size_t size, boolean_t copy, vm_prot_t permissions) {
    vm_address_t address = destination; vm_prot_t current, maximum;
    kern_return_t result = vm_remap(mach_task_self(), &address, size, 0, VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE,
        mach_task_self(), (vm_address_t)source, copy, &current, &maximum, VM_INHERIT_NONE);
    if (result != KERN_SUCCESS || address != destination || (current & permissions) != permissions)
        hy_fail("signed page remapping failed (%d)", result);
    if ((permissions & VM_PROT_EXECUTE) && (current & VM_PROT_WRITE)) hy_fail("unexpected writable code mapping");
}
void hy_map_signed_image(HyModule *m) {
    char text_name[17], patch_name[17], writable_name[17];
    snprintf(text_name, sizeof text_name, "__dm%sx0", m->name);
    snprintf(patch_name, sizeof patch_name, "__dm%sx1", m->name);
    snprintf(writable_name, sizeof writable_name, "__DM%sW", m->name);
    const struct mach_header_64 *header = guest_image();
    unsigned long text_size, patch_size, writable_size;
    const uint8_t *text = getsectiondata(header, "__TEXT", text_name, &text_size);
    const uint8_t *patch = getsectiondata(header, "__TEXT", patch_name, &patch_size);
    const uint8_t *writable = getsectiondata(header, writable_name, "__img", &writable_size);
    if (!text || !patch || !writable || !text_size || !patch_size || !writable_size ||
        ((text_size | patch_size | writable_size | (uintptr_t)text | (uintptr_t)patch | (uintptr_t)writable) & 0x3fff) ||
        text_size != ((m->text_end + 0x3fff) & ~(size_t)0x3fff) || text_size + writable_size != m->patch)
        hy_fail("%s: signed image layout mismatch", m->name);
    m->size = m->patch + patch_size;
    vm_address_t address = 0;
    if (vm_allocate(mach_task_self(), &address, m->size, VM_FLAGS_ANYWHERE) != KERN_SUCCESS)
        hy_fail("signed image address reservation failed");
    remap(address, text, text_size, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
    remap(address + text_size, writable, writable_size, TRUE, VM_PROT_READ | VM_PROT_WRITE);
    remap(address + m->patch, patch, patch_size, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
    m->base = (uint8_t *)address;
}
#endif
