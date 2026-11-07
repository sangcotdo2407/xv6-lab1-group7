#!/usr/bin/env python3
"""Clean build, make qemu, filesystem data checks, and collector fault injection.

All added programs and fault hooks live in disposable build copies only.
Run the main suites with run_file_info.py and run_process_info.py as well.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from run_file_info import ROOT, Console, prepare


def add_program(build, source, name):
    shutil.copy2(ROOT / 'tests' / source, build / 'user' / (name + '.c'))
    path = build / 'Makefile'
    marker = '\t$U/_sysinfotest\\\n'
    text = path.read_text()
    if text.count(marker) != 1:
        raise RuntimeError('expected one sysinfotest entry in UPROGS')
    path.write_text(text.replace(marker, marker + '\t$U/_' + name + '\\\n'))


def inject_faults(build):
    for name, function in [('file.c', 'collect_file_info'),
                           ('proc.c', 'collect_process_info')]:
        path = build / 'kernel' / name
        text = path.read_text()
        marker = function + '(struct sysinfo *info)\n{'
        if text.count(marker) != 1:
            raise RuntimeError('expected one collector definition: ' + function)
        path.write_text(text.replace(marker, marker +
            '\n  static int fail_once;\n  if(!fail_once++) return -1;', 1))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpus', nargs='+', type=int, default=[1, 3])
    parser.add_argument('--output', type=Path, default=ROOT / 'test-results/lab1-extra')
    args = parser.parse_args()
    if any(cpu < 1 or cpu > 8 for cpu in args.cpus):
        parser.error('CPU counts must be between 1 and NCPU=8')
    args.output.mkdir(parents=True, exist_ok=True)
    summary = {'source': str(ROOT), 'results': []}
    summary['source_sha256'] = {
        str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
        for directory in ('kernel', 'user')
        for path in sorted((ROOT / directory).iterdir()) if path.is_file()
    }
    for variant in ('production', 'fault'):
        with tempfile.TemporaryDirectory(prefix='xv6-lab1-' + variant + '-') as directory:
            build = Path(directory)
            prepare(build, None)
            if variant == 'fault':
                inject_faults(build)
                add_program(build, 'sysinfo_fault.c', 'sifault')
            else:
                add_program(build, 'stressfs_check.c', 'stresscheck')
            with (args.output / ('build-' + variant + '.log')).open('w') as log:
                for command in (['make', 'clean'],
                                ['make', '-j4', 'kernel/kernel', 'fs.img']):
                    log.write('$ ' + ' '.join(command) + '\n')
                    log.flush()
                    subprocess.run(command, cwd=build, stdout=log,
                                   stderr=subprocess.STDOUT, check=True)
            headers = subprocess.check_output(
                ['riscv64-unknown-elf-readelf', '-lW', 'user/_sysinfotest'],
                cwd=build, text=True)
            (args.output / ('elf-' + variant + '.txt')).write_text(headers)
            pristine = build / 'pristine.img'
            shutil.copy2(build / 'fs.img', pristine)
            for cpus in args.cpus:
                shutil.copy2(pristine, build / 'fs.img')
                with (args.output / f'qemu-{variant}-cpu{cpus}.log').open('w') as log:
                    console = Console(build, cpus, log, make_qemu=True)
                    try:
                        console.expect(r'\$ ')
                        print(f'PASS: make qemu {variant}, CPUS={cpus}', flush=True)
                        if variant == 'fault':
                            console.command('sifault', 'SYSINFO_FAULT ALL PASSED')
                        else:
                            console.command('stressfs', 'read')
                            console.command('stresscheck', 'STRESSFS_CHECK ALL PASSED')
                            output = console.command('sysinfotest', ' PASS =====', timeout=300)
                            counts = re.search(r'===== (\d+)/(\d+) PASS =====', output)
                            if not counts or counts[1] != counts[2]:
                                raise RuntimeError('sysinfotest did not pass all checks')
                        console.process.stdin.write(b'\x01x')
                        console.process.stdin.flush()
                        if console.process.wait(timeout=10) != 0:
                            raise RuntimeError('make qemu exited with an error')
                    finally:
                        console.close()
                summary['results'].append({'variant': variant, 'cpus': cpus, 'passed': True})
                (args.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print('LAB1_EXTRA ALL PASSED', flush=True)


if __name__ == '__main__':
    main()
