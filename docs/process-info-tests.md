# Kiểm thử Process Management & CPU Scheduling

Thực hiện ngày 06/11/2026 trên `feature/process-info`, nền `1e90d3a`.
Môi trường: WSL Ubuntu 24.04, `riscv64-unknown-elf-gcc` 13.2.0,
GNU ld 2.42, QEMU 8.2.2, RAM guest 128 MiB. Không cài thêm gói hệ thống.

## Cách chạy lại

Trong Linux/WSL tại thư mục repository:

```sh
# Kiểm thử collector trực tiếp trong kernel + bộ hồi quy xv6.
python3 tests/run_process_info.py --toolprefix riscv64-unknown-elf- --cpus 1 3 --quick-suite

# Cùng bộ hồi quy trên kernel production, không gắn selftest.
python3 tests/run_process_info.py --stock --toolprefix riscv64-unknown-elf- --cpus 1 3 --quick-suite

# Kiểm tra vị trí chèn mã test và xử lý nguồn thay đổi.
python3 -m unittest discover -s tests -p 'test_*.py'
```

Trên Windows, mở shell WSL tại thư mục repository và chạy các lệnh trên.
Script dùng thư viện chuẩn Python; yêu cầu sẵn make, host GCC, perl, RISC-V GCC,
binutils và `qemu-system-riscv64` trong Linux.

Mỗi lệnh sao chép nguồn vào thư mục tạm riêng, build `kernel/kernel` và `fs.img`,
rồi chạy QEMU. Mỗi cấu hình CPU bắt đầu bằng ảnh filesystem sạch giống nhau.
Thư mục tạm tự xóa sau khi chạy; chỉ log trong `test-results/process-info/` còn
lại. Thư mục log và Python bytecode được `.gitignore` bỏ qua.

Chế độ mặc định chỉ chèn hai điểm gọi test trong bản sao `forkret()` và ghép
`tests/process_info_kernel.c` vào cuối bản sao `proc.c`. Nhờ đó test gọi được
`allocproc()/freeproc()` static mà không xuất thêm API, không thêm syscall,
không phụ thuộc `collect_file_info()` hoặc phần tích hợp của Thi. `--stock`
không thực hiện thao tác chèn nào. Script giới hạn việc chèn trong định nghĩa
`forkret()`, không phụ thuộc câu comment hoặc chuỗi `usertrapret()`. Nếu thiếu
định nghĩa, có nhiều barrier hoặc sai thứ tự khởi tạo, script báo RuntimeError
trước khi build. Đây không phải C parser tổng quát: khi forkret đổi cấu trúc,
cần cập nhật test có chủ đích. Nguồn repository và Makefile production không đổi.

`--toolprefix` chọn rõ compiler; nếu bỏ qua, thứ tự dò giống Makefile. Mỗi lượt
lưu `build-info-{stock,selftest}.json` gồm phiên bản GCC/ld/QEMU và kết quả
`readelf -lW user/_usertests`, để đối chiếu lỗi do layout ELF giữa các máy.

Nếu bỏ `--quick-suite`, script chạy các ca hồi quy chọn lọc: `preempt`,
`exitwait`, `reparent`, `forkfork`, `sharedfd`, `pipe1`, `sbrkfail`.
Hai lệnh được ghi nhận trong lần bàn giao này đều dùng `--quick-suite`.

## Kết quả thực tế

| Bản kernel | CPU | Selftest kernel | `forktest` | `usertests -q` |
| --- | ---: | --- | --- | --- |
| Có gắn selftest | 1 | PASS | PASS | PASS, 60 ca |
| Có gắn selftest | 3 | PASS | PASS | PASS, 60 ca |
| Production nguyên trạng | 1 | Không gắn | PASS | PASS, 60 ca |
| Production nguyên trạng | 3 | Không gắn | PASS | PASS, 60 ca |

Cả hai build riêng sau sửa review đều thành công với `-Wall -Werror`.
Bốn test Python của instrumentation đều PASS.
Không phát sinh cảnh báo C ở các phần sửa. Linker có cảnh báo `LOAD segment
with RWX permissions` đối với `user/initcode.out`, `kernel/kernel` và
`user/_forktest`; không thay đổi linker script trong công việc này.

Các log QEMU đều có `fork test OK` và `ALL TESTS PASSED`. Hai log selftest có
thêm `PROCESS_INFO ALL PASSED`. Không có `panic:` hoặc `PROCESS_INFO FAIL:`.
Một số ca âm của usertests cố ý gây user page fault và được đánh dấu `OK`;
đây không phải kernel panic.

Log lượt sửa review lưu trong `test-results/pr4-review-fixes/`, được bỏ qua khi
commit; các tên file chính:

- `build-selftest.log`, `build-stock.log`
- `build-info-selftest.json`, `build-info-stock.json`
- `qemu-selftest-cpu1.log`, `qemu-selftest-cpu3.log`
- `qemu-stock-cpu1.log`, `qemu-stock-cpu3.log`

## Các ca kiểm thử kernel đã chạy

| Nhóm | Điều kiện và phép kiểm tra | Kết quả |
| --- | --- | --- |
| Buffer | `NULL` trả `-1`; canary hai đầu không đổi; phần file giữ nguyên; các phần tử dư bằng 0. | PASS, 1/3 CPU |
| Process | Bỏ `UNUSED`; init đang `RUNNING`, không có cha; giữ `USED`, `SLEEPING`, `ZOMBIE`; PID cha đúng. | PASS, 1/3 CPU |
| Giới hạn | Cấp đủ 100 file, 63 proc bổ sung ngoài init, điền đủ 16 fd; hết ô trả lỗi; đúng chỉ số file 0 và 99. | PASS, 1/3 CPU |
| Độ rộng | Chép chính xác `scheduling_count = 2^40 + 7`, không bị cắt xuống 32 bit. | PASS, 1/3 CPU |
| Con trỏ file | Địa chỉ trước mảng, sai căn chỉnh và ngay sau mảng đều trả `-1`, không dereference, xóa phần process khi thất bại. | PASS, 1/3 CPU |
| Helper chung | Kiểm tra đủ 100 index và NULL, trước bảng, lệch 1/8 byte, cuối bảng lệch một byte, ngay sau bảng, UINT64_MAX; test không biết layout `ftable`. | PASS, 1/3 CPU |
| Con trỏ cha | Địa chỉ sai căn chỉnh và ngay sau `proc[]` đều bị từ chối, nhả hết khóa. | PASS, 1/3 CPU |
| Vòng đời | `allocproc()`/`freeproc()` reset 0; cấp lại cùng ô có PID mới và bộ đếm mới. | PASS, 1/3 CPU |
| Dispatch | Tắt timer preemption giữa các phép đo; 32 lần `yield()` thật, mỗi lần được chạy lại bộ đếm tăng chính xác 1; collector không tự tăng bộ đếm. | PASS, 1/3 CPU |
| Fork thật | Con bắt đầu ở `forkret()` với bộ đếm đúng 1, không kế thừa cha; PID cha đúng; fd cha/con trỏ cùng file index. | PASS, 1/3 CPU |
| Sleep thật | Con sleep trên kênh test; cha yield và chụp 16 lần, bộ đếm con không đổi khi chưa wakeup. | PASS, 1/3 CPU |
| Writer đồng thời | Con chạy 200 vòng gọi handler thật `sys_dup/sys_close/sys_pipe`; cha chụp tối thiểu 500 snapshot và kiểm tra bounds/bộ đếm không giảm. | PASS, 1/3 CPU; 500 snapshot mỗi lượt |
| Rollback pipe | Hết fd, chỉ còn một fd trống, copyout với địa chỉ `MAXVA` thất bại, và cấp pipe thành công; đóng hết descriptor sau mỗi vòng. | PASS, 1/3 CPU |
| Exit/wait | Con thoát, `ZOMBIE` không còn fd; bộ đếm zombie ổn định; `wait()` thu hồi đúng PID; PID biến mất khỏi snapshot. | PASS, 1/3 CPU |
| Exit với FD trống/alias | Trước exit con giữ FD 2/15 cùng một file, FD 8 là file riêng, các FD khác trống. Snapshot có đúng 3 FD và index khớp; sau exit ref file chung giảm 3→1, file riêng 1→0, không panic hoặc kẹt khóa. | PASS, 1/3 CPU; lượt review mới nhất |

Các ca hồi quy liên quan trong 60 ca xv6 gồm `forktest`, `preempt`, `exitwait`,
`reparent`, `reparent2`, `twochildren`, `forkfork`, `forkforkfork`, `sharedfd`,
`pipe1`, `killstatus`, `openiput`, `exitiput`, `sbrkfail` và các ca file/memory.
`usertests` cũng kiểm tra số trang rảnh trước/sau suite.

### Chạy lại theo review mới nhất tại `6bac13e`

Lượt này giữ nguyên toàn bộ kernel production tại `6bac13e`, chỉ bổ sung ca
exit/alias nêu trên vào kernel selftest và làm rõ tài liệu. Không lấy kết quả
PASS cũ để thay cho việc chạy lại. Log mới nằm riêng trong
`test-results/pr4-latest-review/`; log tích hợp và đối chứng ELF ở phần sau là
của lượt trước, không phải lượt chạy mới.

| Lượt chạy mới | CPU | Build | Selftest | `forktest` | `usertests -q` |
| --- | ---: | --- | --- | --- | --- |
| Gắn selftest mới | 1 | PASS | PASS | PASS | 60 ca PASS |
| Gắn selftest mới | 3 | PASS | PASS | PASS | 60 ca PASS |
| Production `6bac13e` | 1 | PASS | Không gắn | PASS | 60 ca PASS |
| Production `6bac13e` | 3 | PASS | Không gắn | PASS | 60 ca PASS |

4 unit test instrumentation PASS. Cả bốn log có `fork test OK` và
`ALL TESTS PASSED`; hai log selftest có thêm `PROCESS_INFO ALL PASSED` và
`sparse exit, aliased FDs, exact reference cleanup`. Không có kernel panic.
`git diff origin/main --check` đạt cho toàn bộ thay đổi trong nhánh và working
tree. Linker vẫn có cảnh báo RWX đã ghi ở lượt trước, không có lỗi biên dịch.

Ca mới chặn worker trước khi exit để xác nhận FD 0/1 trống, có lỗ giữa các FD
và slot cuối còn mở; snapshot phải giữ hai FD trùng file_index. Sau đó cho con
exit, xác nhận không còn FD, ref chính xác, bộ đếm zombie ổn định và wait thu
hồi được con. Đọc ref trực tiếp chỉ ở hai pha không có writer: worker đang bị
chặn hoặc đã ZOMBIE, còn init là tiến trình duy nhất có thể thay đổi bảng file.
Không thêm extern ftable hoặc API truy cập reference count vào production.

`usertests -q` có cả `copyout`, `sharedfd`, `exitwait`, `exitiput` và `openiput`;
các ca inode bổ sung kiểm tra đường đóng file thực tế ngoài các file FD_NONE
của selftest. Toolchain vẫn là unknown-elf 13.2/ld 2.42/QEMU 8.2.2; không
diễn giải kết quả này thành kết quả chạy trên linux-gnu của reviewer.

## Review và giới hạn

Đã rà lại toàn bộ diff: vị trí tăng/reset bộ đếm, thứ tự `wait_lock/p->lock`,
không giữ hai khóa process cùng lúc, phạm vi mảng, kiểm tra căn chỉnh con trỏ,
mọi nhánh nhả khóa và các chỗ ghi `ofile[]`. Không sửa `sched()/yield()/fork()`
hoặc `swtch.S`, không tạo `struct sysinfo` trên kernel stack.

## Tích hợp kiểm chứng trong bản sao tạm

Giữ các file process/helper hiện tại; lấy syscall, user API, test và nguyên
`collect_file_info()` từ PR #3 tại `0324941` (bao gồm PR #2 tại `1700d09`).
Chỉ giữ một helper mới tại file.c; không sửa các nhánh thành viên.

| Kiểm tra | 1 CPU | 3 CPU |
| --- | --- | --- |
| `filetest` | PASS | PASS |
| `sysinfotest` | 84/84 PASS | 84/84 PASS |
| `forktest` | PASS | PASS |
| 4 worker dup/close, 10.000 snapshot kiểm tra bounds | Không chạy | PASS |

Log: `build-integration.log`, `qemu-integration-cpu{1,3}.log` trong thư mục kết
quả nêu trên. Đây là kiểm chứng module, chưa phải merge PR hoặc sửa syscall.
Caller của Thi vẫn cần xử lý `-1` từ collector theo review PR #2.

## Đối chiếu copyout và layout ELF

Máy kiểm thử chưa có `riscv64-linux-gnu`; không kết luận đã tái hiện chính xác
toolchain của reviewer. Với GCC unknown-elf 13.2/ld 2.42 hiện có, readelf cho
text ở địa chỉ 0 thuộc LOAD `R E`, data/bss thuộc LOAD `RW`.

| Nguồn | Layout usertests | `usertests copyout`, 1 CPU |
| --- | --- | --- |
| main gốc `1e90d3a` | RX + RW mặc định | PASS |
| Bản sửa PR #4 | RX + RW mặc định | PASS trong suite đầy đủ |
| main gốc `1e90d3a` | Một LOAD RWE, chỉ relink usertests bằng `ld -N` | FAIL, scause 0x2 (illegal instruction) |
| Bản sửa PR #4 | Một LOAD RWE, cùng cách relink | FAIL, cùng scause 0x2 |

Đây là chẩn đoán trong bản build tạm: không đổi linker script hoặc Makefile
production. Khi ELF cho phép ghi text, copyout vào địa chỉ 0 có thể ghi đè code
của test, làm mất giả định của ca kiểm thử. Reviewer thấy scause 0xc, còn mô
phỏng thấy 0x2: thí nghiệm xác nhận nguy cơ do text ghi được, chưa chứng minh
nguyên nhân chính xác trên máy reviewer. Tùy chọn `-z noseparate-code` riêng lẻ không
đổi layout trên toolchain này; thí nghiệm RWE dùng `-N` sau khi đã xác nhận
program headers thực tế.

Log: `copyout-comparison.json`, `copyout-*.elf.txt` và
`qemu-copyout-{baseline-default,baseline-rwe,fixed-rwe}.log`. Trên máy khác cần
ghi lại cả compiler, linker và ELF; không suy ra mọi toolchain linux-gnu đều lỗi.

## Giới hạn kiểm thử

- Đã chạy syscall/test trong bản ghép tạm, nhưng chưa fault-inject lỗi collector
  qua syscall. MAXVA trong kernel selftest thuộc sys_pipe; các ca địa chỉ user
  của sysinfo nằm trong sysinfotest ở bản ghép.
- Chưa chạy sáu ca `slowtests` ngoài `usertests -q`, stress dài hạn, sanitizer,
  hoặc fault injection làm `kalloc()` thất bại ngay trong `allocproc()`. Đã có
  kiểm tra hết `NPROC` và ca memory/fork lỗi trong suite gốc.
- `reparent/reparent2` đã chạy hồi quy, nhưng collector chưa được đo đồng thời
  riêng với một workload reparent liên tục. Snapshot hiện không nguyên tử toàn
  hệ thống, đặc biệt giữa phần file và process.
- PR #3 còn cần thay phép tính index nội bộ của file collector bằng helper đã
  kiểm tra căn chỉnh; bản ghép chỉ xác nhận workload có con trỏ hợp lệ.
- `uint64` có thể wrap sau 2^64 dispatch; không thực nghiệm đến ngưỡng đó.

Không phát hiện lỗi chức năng trong phạm vi đã kiểm tra. Những kết quả trên
không thay thế kiểm thử tích hợp toàn bộ syscall và review của thành viên khác
theo `docs/review-pr.md`.
