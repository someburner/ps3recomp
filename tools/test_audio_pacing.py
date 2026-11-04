"""Compile actual audio mixer/ring fixtures and run without a physical device."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
root=Path(__file__).resolve().parents[1]
flags=shlex.split(subprocess.check_output(['sdl2-config','--cflags','--libs'],text=True))
flags+=['-I'+str(Path(x[2:]).parent) for x in flags if x.startswith('-I') and Path(x[2:]).name=='SDL2']
dead=['-Wl,-dead_strip'] if sys.platform=='darwin' else ['-Wl,--gc-sections']
with tempfile.TemporaryDirectory(prefix='audio-pacing-') as tmp:
    for fixture in ('test_audio_pacing','test_audio_ring'):
        exe=Path(tmp)/fixture
        subprocess.run([os.environ.get('CC','cc'),'-std=gnu17','-O2','-UNDEBUG',
                        '-ffunction-sections','-fdata-sections','-Iinclude',
                        'libs/audio/tests/'+fixture+'.c',*flags,*dead,'-pthread','-lm','-o',str(exe)],cwd=root,check=True)
        subprocess.run([str(exe)],check=True,timeout=30)
