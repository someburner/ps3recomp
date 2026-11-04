#!/usr/bin/env python3
"""Exercise real dispatch ownership and late registration with negative caching."""
import os, subprocess, tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]; spu=root/'runtime/spu'
sources=['spu_channels.c','spu_drain.c','spu_lockstep.c','spu_coherency.c','spu_workload.c',
    'spurs_policy.c','spurs_job.c','spu_tsp_weak.c','spu_vm_pagemap.c','spurs_policy_blob_weak.c',
    'spu_interp.c','spu_lifted_thread.c']
with tempfile.TemporaryDirectory(prefix='spu-negative-cache-') as tmp:
    exe=Path(tmp)/'fixture'
    subprocess.run([os.environ.get('CC','cc'),'-std=gnu17','-O2','-UNDEBUG',
        '-I'+str(root/'include'),'-I'+str(spu),'-I'+str(root/'runtime/platform'),
        str(spu/'tests/test_spu_lifted_start.c'),*[str(spu/s) for s in sources],
        str(root/'runtime/syscalls/spu_fallback.c'),str(root/'runtime/syscalls/sdk_version_weak.c'),
        str(root/'runtime/platform/win32_compat.c'),'-pthread','-lm','-o',str(exe)],check=True)
    for mode,bitmap in (('0','0'),('1','0'),('1','1'),('shadow','1')):
        env=dict(os.environ,SPU_DISPATCH_CACHE=mode,SPU_DISPATCH_CACHE_MISSES='1',SPU_INTERP_DECODE_CACHE='1',SPU_DISPATCH_ENTRY_BITMAP=bitmap)
        result=subprocess.run([str(exe)],cwd=tmp,env=env,capture_output=True,text=True,timeout=60)
        print('mode',mode,'bitmap',bitmap,result.stdout.splitlines()[-1] if result.stdout else 'no output',flush=True)
        if result.returncode or 'shadow mismatch' in result.stderr:
            print(result.stdout);print(result.stderr);raise SystemExit(1)
