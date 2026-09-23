"""Exercise production configuration indexing without board/UI dependencies."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]


def extract(path, signature):
    source = path.read_text()
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


with tempfile.TemporaryDirectory(prefix='arcade-pad-config-') as directory:
    tmp = Path(directory)
    (tmp / 'button_set.inc').write_text(
        extract(root / 'pad_manager.h', 'struct ButtonSet') + ';\n')
    (tmp / 'button_config.inc').write_text(''.join(
        extract(root / 'pad_manager.cpp', signature) for signature in [
            'int PadManager::ButtonConfigMode::nButtons(',
            'void PadManager::ButtonConfigMode::next(',
            'void PadManager::ButtonConfigMode::appendUnit(',
            'void PadManager::AnalogConfigMode::next(',
        ]))
    binary = tmp / 'pad_config_test'
    subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
        '-std=c++17', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=address,undefined', '-I.', '-I' + str(tmp),
        'tests/pad_config_test.cpp', '-o', str(binary),
    ], cwd=root, check=True)
    subprocess.run([str(binary)], check=True,
                   env={**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1'})
