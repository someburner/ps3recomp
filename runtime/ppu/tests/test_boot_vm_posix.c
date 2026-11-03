#include "boot_vm_posix.h"
#include "../../memory/vm.h"
#include <assert.h>
#include <stdio.h>

uint8_t* vm_base;
uint32_t ppu_vm_size;

int main(void)
{
    const size_t size = UINT64_C(0x100010000);
    vm_base = boot_map_flat_vm(size);
    assert(vm_base && ((uintptr_t)vm_base & UINT32_MAX) == 0);
    const uint32_t addresses[] = {0, 0x10000000, 0xD0000000, 0xFFFFFFFF};
    for (unsigned i = 0; i < sizeof(addresses)/sizeof(addresses[0]); ++i) {
        uint32_t ea = addresses[i];
        assert(vm_base[ea] == 0);
        vm_base[ea] = 0x5A;
        assert((uint32_t)(uintptr_t)(vm_base + ea) == ea);
    }
    assert(vm_commit(0x10000000, 0x100000) == CELL_OK);
    assert(vm_commit(0x11100000, 0x7400000) == CELL_OK);
    assert(vm_base[UINT64_C(0x100000007)] == 0);
    assert(munmap(vm_base, size) == 0);
    puts("PASS: aligned boot mapping, LV2 memory commits, pointer round trip, top-edge guard");
    return 0;
}
