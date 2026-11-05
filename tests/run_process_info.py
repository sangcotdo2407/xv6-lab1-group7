#!/usr/bin/env python3
"""Build in a disposable directory, run kernel tests and stock xv6 regressions.

Requires Linux/WSL, make, GCC/binutils for RISC-V, and qemu-system-riscv64.
No sysinfo syscall or collect_file_info implementation is required.
"""

import argparse
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
    def __init__(self, build, cpus, log):
        self.log = log
        self.text = ""
        self.cursor = 0
        self.process = subprocess.Popen(
            ["qemu-system-riscv64", "-machine", "virt", "-bios", "none",
             "-kernel", "kernel/kernel", "-m", "128M", "-smp", str(cpus),
             "-nographic", "-monitor", "none",
             "-global", "virtio-mmio.force-legacy=false",
             "-drive", "file=fs.img,if=none,format=raw,id=x0",
             "-device", "virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0"],
            cwd=build, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)

    def expect(self, pattern, timeout=120):
        deadline = time.monotonic() + timeout
        while True:
            remaining = self.text[self.cursor:]
            if "panic:" in remaining or "PROCESS_INFO FAIL:" in remaining:
                raise RuntimeError(remaining[-3000:])
            match = re.search(pattern, remaining)
            if match:
                self.cursor += match.end()
                return remaining[:match.end()]
            if time.monotonic() >= deadline:
                raise TimeoutError(f"waiting for {pattern!r}\n{remaining[-3000:]}")
            for key, _ in self.selector.select(timeout=0.5):
                data = os.read(key.fileobj.fileno(), 65536)
                if not data:
                    raise RuntimeError(f"QEMU exited: {self.process.poll()}")
                chunk = data.decode("utf-8", errors="replace").replace("\r", "")
                self.text += chunk
                self.log.write(chunk)
                self.log.flush()

    def command(self, command, success, timeout=120):
        self.process.stdin.write((command + "\n").encode())
        self.process.stdin.flush()
        result = self.expect(r"\$ ", timeout)
        if success not in result or "SOME TESTS FAILED" in result:
            raise RuntimeError(f"{command} failed:\n{result[-4000:]}")
        print(f"PASS: {command}", flush=True)

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


def prepare(build, instrument):
    # Source files only: never copy .git, editor files or existing artifacts.
    for directory in ("kernel", "user", "mkfs"):
        target = build / directory
        target.mkdir()
        for source in (ROOT / directory).iterdir():
            if source.is_file() and source.suffix in (".c", ".h", ".S", ".ld", ".pl"):
                if source.name != "usys.S":
                    shutil.copy2(source, target / source.name)
    for name in ("Makefile", "README"):
        shutil.copy2(ROOT / name, build / name)
    if instrument:
        path = build / "kernel/proc.c"
        source = path.read_text()
        declarations = ("static void process_info_selftest(void);\n"
                        "static void process_info_test_worker(void);\n")
        source = declarations + source
        anchor = "  // Still holding p->lock from scheduler."
        assert source.count(anchor) == 1
        source = source.replace(anchor,
            '  if(strncmp(myproc()->name, "pinfo-worker", 16) == 0)\n'
            '    process_info_test_worker();\n\n' + anchor)
        anchor = "    __sync_synchronize();\n  }\n\n  usertrapret();"
        assert source.count(anchor) == 1
        source = source.replace(anchor,
            "    __sync_synchronize();\n    process_info_selftest();\n  }\n\n"
            "  usertrapret();")
        source += "\n" + (ROOT / "tests/process_info_kernel.c").read_text()
        path.write_text(source)


def run(build, cpus, output, instrument, quick):
    suffix = "selftest" if instrument else "stock"
    path = output / f"qemu-{suffix}-cpu{cpus}.log"
    with path.open("w") as log:
        console = Console(build, cpus, log)
        try:
            console.expect(r"\$ ")
            if instrument and "PROCESS_INFO ALL PASSED" not in console.text:
                raise RuntimeError("kernel selftests did not run")
            print(f"PASS: boot {suffix}, CPUS={cpus}", flush=True)
            console.command("forktest", "fork test OK")
            if quick:
                console.command("usertests -q", "ALL TESTS PASSED", timeout=600)
            else:
                for test in ("preempt", "exitwait", "reparent", "forkfork",
                             "sharedfd", "pipe1", "sbrkfail"):
                    console.command(f"usertests {test}", "ALL TESTS PASSED", timeout=180)
        finally:
            console.close()
    print(f"Log: {path}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpus", nargs="+", type=int, default=[1, 3])
    parser.add_argument("--stock", action="store_true",
                        help="run the production kernel without test instrumentation")
    parser.add_argument("--quick-suite", action="store_true",
                        help="run all stock usertests -q cases instead of selected cases")
    parser.add_argument("--output", type=Path, default=ROOT / "test-results/process-info")
    args = parser.parse_args()
    if any(cpu < 1 or cpu > 8 for cpu in args.cpus):
        parser.error("CPU counts must be between 1 and NCPU=8")
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="xv6-process-info-") as directory:
        build = Path(directory)
        prepare(build, instrument=not args.stock)
        build_log = args.output / ("build-stock.log" if args.stock else "build-selftest.log")
        with build_log.open("w") as log:
            subprocess.run(["make", "-j4", "kernel/kernel", "fs.img"], cwd=build,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        print(f"PASS: build ({build_log})", flush=True)
        for cpus in args.cpus:
            # Every QEMU run starts from the same pristine filesystem image.
            pristine = build / "fs-pristine.img"
            if not pristine.exists():
                shutil.copy2(build / "fs.img", pristine)
            shutil.copy2(pristine, build / "fs.img")
            run(build, cpus, args.output, instrument=not args.stock,
                quick=args.quick_suite)


if __name__ == "__main__":
    main()
