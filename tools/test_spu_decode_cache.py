#!/usr/bin/env python3
"""Compare the real cached decoder with the uncached decoder and run execution fixtures."""
import os, subprocess, tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
spu=root/'runtime/spu'
prefix=(spu/'spu_interp.c').read_text().split('/* spu_lifted_lookup()')[0]
driver=r'''
#include <assert.h>
#include <pthread.h>
static void compare(uint32_t word,uint32_t pc) {
    spu_ins a,b;
    spu_decode1(word,pc,&a); spu_decode_cached(word,pc,&b);
    assert(a.op==b.op && a.rt==b.rt && a.ra==b.ra && a.rb==b.rb &&
           a.rc==b.rc && a.ch==b.ch && a.imm==b.imm && a.tgt==b.tgt);
}
static void* run(void* arg) {
    uint32_t rng=(uint32_t)(uintptr_t)arg+1234;
    for(unsigned i=0;i<1000000;i++) {
        rng=rng*1664525u+1013904223u;
        uint32_t pc=(i*4u)&0x3fffcu;
        compare(rng,pc); compare(rng,pc);             /* miss then hit */
        compare(rng^0x8000u,pc);                    /* changed live word */
        compare(rng,pc^0x4000u); compare(rng,pc);   /* cache collision */
    }
    return 0;
}
int main(void) {
    pthread_t threads[4];
    for(uintptr_t i=0;i<4;i++) assert(!pthread_create(&threads[i],0,run,(void*)i));
    for(unsigned i=0;i<4;i++) assert(!pthread_join(threads[i],0));
    puts("PASS: 20 million cached/uncached decode comparisons, changed words, PC collisions and concurrent threads");
}
'''
with tempfile.TemporaryDirectory(prefix='spu-decode-cache-') as tmp:
    tmp=Path(tmp); source=tmp/'decoder.c'; source.write_text(prefix+driver)
    exe=tmp/'decoder'
    flags=[os.environ.get('CC','cc'),'-std=gnu17','-O2','-UNDEBUG','-I'+str(root/'include'),'-I'+str(spu)]
    subprocess.run([*flags,str(source),'-pthread','-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe)],env=dict(os.environ,SPU_INTERP_DECODE_CACHE='1'),check=True,timeout=30)
    for fixture in ('test_spu_interpreter_ilh','spu_interp_selftest'):
        exe=tmp/fixture
        subprocess.run([*flags,str(spu/'tests'/(fixture+'.c')),str(spu/'spu_interp.c'),'-lm','-o',str(exe)],check=True)
        for mode in ('0','1'):
            subprocess.run([str(exe)],cwd=tmp,env=dict(os.environ,SPU_INTERP_DECODE_CACHE=mode),check=True,timeout=10)
