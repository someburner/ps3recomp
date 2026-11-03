#pragma once
#ifndef _WIN32
#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>

/* Flat, zero-filled guest RAM, lazily backed by the OS. Match vm.h's 4 GB
 * alignment contract for HLE host-pointer -> guest-address truncation, and
 * retain the boot harness's extra guard span for reads at the 32-bit edge.
 * calloc is not page-aligned, so vm_commit's mprotect fails even on Linux. */
static inline uint8_t* boot_map_flat_vm(size_t size)
{
    const uintptr_t alignment = UINT64_C(0x100000000);
    const size_t reserve = size + alignment;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_NORESERVE
    flags |= MAP_NORESERVE;
#endif
    uint8_t* raw = (uint8_t*)mmap(NULL, reserve, PROT_READ | PROT_WRITE,
                                flags, -1, 0);
    if (raw == MAP_FAILED) return NULL;
    uintptr_t address = ((uintptr_t)raw + alignment - 1) & ~(alignment - 1);
    uint8_t* aligned = (uint8_t*)address;
    size_t head = address - (uintptr_t)raw;
    size_t tail = reserve - head - size;
    if (head) munmap(raw, head);
    if (tail) munmap(aligned + size, tail);
    return aligned;
}
#endif
