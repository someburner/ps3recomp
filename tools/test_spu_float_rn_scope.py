#!/usr/bin/env python3
"""x86 SPU floating-point scope lifetime and caller-mode preservation."""
import os, platform, subprocess, tempfile
from pathlib import Path
if platform.machine().lower() not in ('x86_64','amd64'):
    print('SKIP: x86-64 fixture'); raise SystemExit(0)
root=Path(__file__).resolve().parents[1]; spu=root/'runtime/spu'
sources=['spu_channels.c','spu_drain.c','spu_lockstep.c','spu_coherency.c',
    'spu_workload.c','spurs_policy.c','spurs_job.c','spu_tsp_weak.c',
    'spu_vm_pagemap.c','spurs_policy_blob_weak.c','spu_interp.c']
with tempfile.TemporaryDirectory(prefix='spu-rn-scope-') as tmp:
    exe=Path(tmp)/'test'
    subprocess.run([os.environ.get('CC','clang'),'-std=gnu17','-O2','-UNDEBUG',
        '-mavx2','-mfma','-ffp-contract=off','-DSPU_X86_FAST_FLOAT',
        '-I'+str(root/'include'),'-I'+str(spu),'-I'+str(root/'runtime/platform'),
        str(spu/'tests/test_spu_float_rz_mode.c'),*[str(spu/s) for s in sources],
        str(root/'runtime/platform/win32_compat.c'),'-pthread','-lm','-o',str(exe)],check=True)
    for mode in ('0','1'):
        subprocess.run([str(exe)],check=True,timeout=30,cwd=tmp,env=dict(os.environ,SPU_FLOAT_RZ=mode))
