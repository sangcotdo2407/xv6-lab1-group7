#!/usr/bin/env python3

import argparse
import json
import os
from pathlib import Path
import re
import selectors
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]

class Console:
    def __init__(self, build, cpus, log, make_qemu=False):
        self.log, self.text, self.cursor = log, '', 0
        command = ['make', 'qemu', f'CPUS={cpus}'] if make_qemu else [
            'qemu-system-riscv64', '-machine', 'virt', '-bios', 'none',
            '-kernel', 'kernel/kernel', '-m', '128M', '-smp', str(cpus),
            '-nographic', '-monitor', 'none',
            '-global', 'virtio-mmio.force-legacy=false',
            '-drive', 'file=fs.img,if=none,format=raw,id=x0',
            '-device', 'virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0',
        ]
        self.process = subprocess.Popen(command, cwd=build, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT)
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)

    def expect(self, pattern, timeout=180):
        deadline = time.monotonic()+timeout
        while True:
            remaining = self.text[self.cursor:]
            if 'panic:' in remaining:
                raise RuntimeError(remaining[-4000:])
            match = re.search(pattern, remaining)
            if match:
                self.cursor += match.end()
                return remaining[:match.end()]
            if time.monotonic() >= deadline:
                raise TimeoutError(f'waiting for {pattern!r}\n{remaining[-4000:]}')
            for key, _ in self.selector.select(timeout=0.5):
                data = os.read(key.fileobj.fileno(), 65536)
                if not data:
                    raise RuntimeError(f'QEMU exited: {self.process.poll()}')
                chunk = data.decode('utf-8', errors='replace').replace('\r', '')
                self.text += chunk
                self.log.write(chunk)
                self.log.flush()

    def command(self, command, success, timeout=180):
        self.process.stdin.write((command+'\n').encode())
        self.process.stdin.flush()
        result = self.expect(r'\$ ', timeout)
        if success not in result or re.search(r'\bFAIL(?:ED)?\b', result):
            raise RuntimeError(f'{command} failed:\n{result[-6000:]}')
        print(f'PASS: {command}', flush=True)
        return result

    def close(self):
        self.process.terminate()
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        self.selector.close()
        self.process.stdin.close()
        self.process.stdout.close()

def prepare(build, process_source, syscall_source=None, kernel_probe=False):
    for directory in ('kernel', 'user', 'mkfs'):
        target = build/directory
        target.mkdir()
        for source in (ROOT/directory).iterdir():
            if source.is_file() and source.suffix in ('.c', '.h', '.S', '.ld', '.pl'):
                if source.name != 'usys.S':
                    shutil.copy2(source, target/source.name)
    for name in ('Makefile', 'README'):
        shutil.copy2(ROOT/name, build/name)
    if process_source:
        for name in ('kernel/proc.c', 'kernel/proc.h', 'kernel/sysfile.c'):
            shutil.copy2(process_source/name, build/name)
    if syscall_source:
        for name in ('kernel/sysproc.c', 'user/sysinfotest.c'):
            shutil.copy2(syscall_source/name, build/name)
    if kernel_probe:
        path = build/'kernel/proc.c'
        source = path.read_text()
        marker = '    __sync_synchronize();'
        if source.count(marker) != 1:
            raise RuntimeError('kernel probe needs the forkret fsinit barrier')
        source = source.replace(marker, marker+'\n    file_info_kernel_test();', 1)
        source = 'static void file_info_kernel_test(void);\n'+source
        source += '\n'+(ROOT/'tests/file_info_kernel.c').read_text()
        path.write_text(source)

def native(output):
    with tempfile.TemporaryDirectory(prefix='xv6-file-native-') as directory:
        for name in ('file_info_native', 'sysinfo_print_native'):
            executable = Path(directory)/name
            with (output/f'{name}.log').open('w') as log:
                subprocess.run([
                    'gcc', '-O1', '-g', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-fno-builtin', '-fno-pie', '-no-pie',
                    '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                    '-I'+str(ROOT), str(ROOT/'tests'/f'{name}.c'),
                    '-o', str(executable),
                ], stdout=log, stderr=subprocess.STDOUT, check=True)
                subprocess.run([str(executable)], stdout=log,
                               stderr=subprocess.STDOUT, check=True)
            print(f'PASS: {name} (ASan/UBSan)', flush=True)

def qemu_suite(build, cpus, output, label, quick, kernel_probe=False):
    with (output/f'qemu-{label}-cpu{cpus}.log').open('w') as log:
        console = Console(build, cpus, log)
        try:
            console.expect(r'\$ ')
            print(f'PASS: boot {label}, CPUS={cpus}', flush=True)
            if kernel_probe:
                if 'FILE_INFO_KERNEL ALL PASSED' not in console.text:
                    raise RuntimeError('kernel probe did not run')
                print('PASS: kernel collector regression', flush=True)
                return
            console.command('filetest', 'filetest: ALL PASSED', timeout=600)
            result = console.command('sysinfotest', ' PASS =====', timeout=240)
            summary = re.search(r'===== (\d+)/(\d+) PASS =====', result)
            if not summary or summary[1] != summary[2]:
                raise RuntimeError('sysinfotest did not pass every check')
            console.command('forktest', 'fork test OK')
            if quick:
                console.command('usertests -q', 'ALL TESTS PASSED', timeout=900)
            else:
                for name in ('preempt', 'sharedfd', 'pipe1', 'exitwait', 'reparent'):
                    console.command('usertests '+name, 'ALL TESTS PASSED')
        finally:
            console.close()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpus', nargs='+', type=int, default=[1, 3])
    parser.add_argument('--quick-suite', action='store_true')
    parser.add_argument('--native-only', action='store_true')
    parser.add_argument('--kernel-probe-only', action='store_true',
                        help='test injected bad pointers in a temporary kernel at boot')
    parser.add_argument('--process-source', type=Path,
                        help='also test with proc.c/proc.h/sysfile.c from a PR #4 checkout')
    parser.add_argument('--syscall-source', type=Path,
                        help='also test with sysproc.c/sysinfotest.c from a PR #2 checkout')
    parser.add_argument('--integration-only', action='store_true',
                        help='skip already-tested native and standalone variants')
    parser.add_argument('--output', type=Path, default=ROOT/'test-results/file-info')
    args = parser.parse_args()
    if any(cpu < 1 or cpu > 8 for cpu in args.cpus):
        parser.error('CPU count must be between 1 and NCPU=8')
    if args.integration_only and not (args.process_source or args.syscall_source):
        parser.error('--integration-only needs at least one module source')
    args.output.mkdir(parents=True, exist_ok=True)
    if not args.kernel_probe_only and not args.integration_only:
        native(args.output)
    if args.native_only:
        return
    metadata = {
        name: subprocess.check_output(command, text=True).splitlines()[0]
        for name, command in [
            ('gcc', ['riscv64-unknown-elf-gcc', '--version']),
            ('qemu', ['qemu-system-riscv64', '--version']),
        ]
    }
    metadata['source'] = str(ROOT)
    metadata['process_source'] = str(args.process_source) if args.process_source else None
    metadata['syscall_source'] = str(args.syscall_source) if args.syscall_source else None
    (args.output/'environment.json').write_text(json.dumps(metadata, indent=2)+'\n')
    variants = [] if args.integration_only else [('fixed', None, None)]
    if args.process_source or args.syscall_source:
        variants.append(('integrated', args.process_source, args.syscall_source))
    for label, process_source, syscall_source in variants:
        if args.kernel_probe_only:
            label += '-probe'
        with tempfile.TemporaryDirectory(prefix='xv6-file-'+label+'-') as directory:
            build = Path(directory)
            prepare(build, process_source, syscall_source, args.kernel_probe_only)
            with (args.output/f'build-{label}.log').open('w') as log:
                subprocess.run(['make', '-j4', 'TOOLPREFIX=riscv64-unknown-elf-',
                                'kernel/kernel', 'fs.img'], cwd=build,
                               stdout=log, stderr=subprocess.STDOUT, check=True)
            print(f'PASS: build {label}', flush=True)
            pristine = build/'fs-pristine.img'
            shutil.copy2(build/'fs.img', pristine)
            for cpus in args.cpus:
                shutil.copy2(pristine, build/'fs.img')
                qemu_suite(build, cpus, args.output, label, args.quick_suite,
                           args.kernel_probe_only)
    print('FILE_INFO ALL RUNS PASSED', flush=True)

if __name__ == '__main__':
    main()
