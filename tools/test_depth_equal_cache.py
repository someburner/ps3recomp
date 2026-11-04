"""Exercise production draw submission with depth conversion retention off/on."""
import os
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
sources=['tests/test_depth_equal_cache.c','rsx_dispatch.c','rsx_vertex_compact.c',
         'rsx_texture_layout.c','rsx_vp_decompiler.c','rsx_fp_decompiler.c']
with tempfile.TemporaryDirectory(prefix='depth-equal-') as directory:
    exe=Path(directory)/'test'
    subprocess.run([os.environ.get('CC','cc'),'-std=gnu17','-O2','-UNDEBUG',
                    '-Iinclude','-Ilibs/video',*[f'libs/video/{s}' for s in sources],
                    '-lm','-o',str(exe)],cwd=root,check=True)
    for mode in ('0','1'):
        subprocess.run([str(exe)],env=dict(os.environ,PS3RECOMP_DEPTH_EQUAL_CACHE=mode),check=True)
