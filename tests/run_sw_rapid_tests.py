"""Exercise the production software rapid timer without Pico dependencies."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'main.cpp').read_text()
start = source.index('class SwRapidFire\n')
end = source.index('\nvoid initDevices()', start)
with tempfile.TemporaryDirectory(prefix='arcade-sw-rapid-') as directory:
    tmp = Path(directory)
    (tmp / 'sw_rapid.inc').write_text(source[start:end])
    binary = tmp / 'sw_rapid_test'
    subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
        '-std=c++17', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=address,undefined', '-I' + str(tmp),
        str(root / 'tests/sw_rapid_test.cpp'), '-o', str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True,
                   env={**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1'})
