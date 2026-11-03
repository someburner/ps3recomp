#!/usr/bin/env python3
"""Execute lifted import stubs and check the guest caller's TOC restoration.

Uses real PPU instruction encodings, two caller TOCs, alternate stack slots,
and a stub without a save. No game assets or firmware are needed.
"""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

from ppu_disasm import disassemble_bytes
from ppu_lifter import PPULifter


def main():
    lifter = PPULifter()
    bodies = []
    for index, offset in enumerate((40, 56, None)):
        start = 0x1000 + index * 32
        words = [0x39800000, 0x658C0096, 0x818C8978,
                 0xF8410000 | offset if offset is not None else 0x60000000,
                 0x800C0000, 0x804C0004, 0x7C0903A6, 0x4E800420]
        lifter.hle_stub_nids[start] = 0x744680A2
        insns = disassemble_bytes(struct.pack('>8I', *words), start)
        fn = lifter.lift_function(insns, start, start + 32)
        bodies.append('void stub%d(ppu_context* ctx) {\n%s\n}' %
                      (index, '\n'.join(fn.body_lines)))
    source = r'''
#include <cstdint>
#include <cassert>
struct ppu_context { uint64_t gpr[32]{}; };
uint64_t memory[64];
void vm_write64(uint64_t addr, uint64_t value) {
    assert(addr % 8 == 0 && addr / 8 < 64);
    memory[addr / 8] = value;
}
void ps3_hle_call(uint32_t nid, ppu_context* ctx) {
    assert(nid == 0x744680A2u);
    ctx->gpr[2] = 0xDEADBEEF;
}
''' + '\n'.join(bodies) + r'''
int main() {
    for (uint64_t toc : {0x96C1F0ull, 0x97C0A0ull}) {
        for (int which = 0; which < 3; ++which) {
            for (auto& slot : memory) slot = 0x12345678;
            ppu_context ctx;
            ctx.gpr[1] = 128;
            ctx.gpr[2] = toc;
            void (*stubs[])(ppu_context*) = {stub0, stub1, stub2};
            stubs[which](&ctx);
            assert(ctx.gpr[1] == 128 && ctx.gpr[2] == 0xDEADBEEF);
            for (int slot = 0; slot < 64; ++slot) {
                int saved = which == 0 ? 21 : which == 1 ? 23 : -1;
                assert(memory[slot] == (slot == saved ? toc : 0x12345678));
            }
            if (which < 2) {
                ctx.gpr[2] = memory[(ctx.gpr[1] + (which == 0 ? 40 : 56)) / 8];
                assert(ctx.gpr[2] == toc);
            }
        }
    }
}
'''
    source = '#include <initializer_list>\n' + source
    compiler = shutil.which(os.environ.get('CXX', 'c++'))
    if compiler is None:
        raise RuntimeError('A C++ compiler is required')
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        (path/'fixture.cpp').write_text(source)
        subprocess.run([compiler, '-std=c++17', str(path/'fixture.cpp'),
                        '-o', str(path/'fixture')], check=True)
        subprocess.run([str(path/'fixture')], check=True)
    print('PASS: import TOC saves, multi-TOC caller restores, and no-save stub')


if __name__ == '__main__':
    main()
