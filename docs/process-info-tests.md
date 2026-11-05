# Kiểm thử Process Management & CPU Scheduling

Thực hiện ngày 06/11/2026 trên `feature/process-info`, nền `1e90d3a`.
Môi trường: WSL Ubuntu 24.04, GCC/binutils `riscv64-unknown-elf`, QEMU RISC-V,
RAM guest 128 MiB. Không cài thêm gói hệ thống.

## Cách chạy lại

Trong Linux/WSL tại thư mục repository:

```sh
# Kiểm thử collector trực tiếp trong kernel + bộ hồi quy xv6.
python3 tests/run_process_info.py --cpus 1 3 --quick-suite

# Cùng bộ hồi quy trên kernel production, không gắn selftest.
python3 tests/run_process_info.py --stock --cpus 1 3 --quick-suite
```

Trên PowerShell, thêm tiền tố
`wsl -d Ubuntu-24.04 -- bash -lc 'cd /mnt/c/Users/Admin/xv6-labs-2023 && ...'`.
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
không thực hiện thao tác chèn nào. Nguồn trong repository và Makefile production
không bị script sửa đổi.

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

Cả hai build riêng đều thành công với `-Wall -Werror`. Ngoài ra đã build trực
tiếp trong workspace bằng `make -j4 kernel/kernel fs.img`, thành công.
Không phát sinh cảnh báo C ở các phần sửa. Linker có cảnh báo `LOAD segment
with RWX permissions` đối với `user/initcode.out`, `kernel/kernel` và
`user/_forktest`; không thay đổi linker script trong công việc này.

Các log QEMU đều có `fork test OK` và `ALL TESTS PASSED`. Hai log selftest có
thêm `PROCESS_INFO ALL PASSED`. Không có `panic:` hoặc `PROCESS_INFO FAIL:`.
Một số ca âm của usertests cố ý gây user page fault và được đánh dấu `OK`;
đây không phải kernel panic.

Log lưu tại:

- `test-results/process-info/build-selftest.log`
- `test-results/process-info/build-stock.log`
- `test-results/process-info/qemu-selftest-cpu1.log`
- `test-results/process-info/qemu-selftest-cpu3.log`
- `test-results/process-info/qemu-stock-cpu1.log`
- `test-results/process-info/qemu-stock-cpu3.log`

## Các ca kiểm thử kernel đã chạy

| Nhóm | Điều kiện và phép kiểm tra | Kết quả |
| --- | --- | --- |
| Buffer | `NULL` trả `-1`; canary hai đầu không đổi; phần file giữ nguyên; các phần tử dư bằng 0. | PASS, 1/3 CPU |
| Process | Bỏ `UNUSED`; init đang `RUNNING`, không có cha; giữ `USED`, `SLEEPING`, `ZOMBIE`; PID cha đúng. | PASS, 1/3 CPU |
| Giới hạn | Cấp đủ 100 file, 63 proc bổ sung ngoài init, điền đủ 16 fd; hết ô trả lỗi; đúng chỉ số file 0 và 99. | PASS, 1/3 CPU |
| Độ rộng | Chép chính xác `scheduling_count = 2^40 + 7`, không bị cắt xuống 32 bit. | PASS, 1/3 CPU |
| Con trỏ file | Địa chỉ trước mảng, sai căn chỉnh và ngay sau mảng đều trả `-1`, không dereference, xóa phần process khi thất bại. | PASS, 1/3 CPU |
| Con trỏ cha | Địa chỉ sai căn chỉnh và ngay sau `proc[]` đều bị từ chối, nhả hết khóa. | PASS, 1/3 CPU |
| Vòng đời | `allocproc()`/`freeproc()` reset 0; cấp lại cùng ô có PID mới và bộ đếm mới. | PASS, 1/3 CPU |
| Dispatch | Tắt timer preemption giữa các phép đo; 32 lần `yield()` thật, mỗi lần được chạy lại bộ đếm tăng chính xác 1; collector không tự tăng bộ đếm. | PASS, 1/3 CPU |
| Fork thật | Con bắt đầu ở `forkret()` với bộ đếm đúng 1, không kế thừa cha; PID cha đúng; fd cha/con trỏ cùng file index. | PASS, 1/3 CPU |
| Sleep thật | Con sleep trên kênh test; cha yield và chụp 16 lần, bộ đếm con không đổi khi chưa wakeup. | PASS, 1/3 CPU |
| Writer đồng thời | Con chạy 200 vòng gọi handler thật `sys_dup/sys_close/sys_pipe`; cha chụp tối thiểu 500 snapshot và kiểm tra bounds/bộ đếm không giảm. | PASS, 1/3 CPU; 500 snapshot mỗi lượt |
| Rollback pipe | Hết fd, chỉ còn một fd trống, copyout với địa chỉ `MAXVA` thất bại, và cấp pipe thành công; đóng hết descriptor sau mỗi vòng. | PASS, 1/3 CPU |
| Exit/wait | Con thoát, `ZOMBIE` không còn fd; bộ đếm zombie ổn định; `wait()` thu hồi đúng PID; PID biến mất khỏi snapshot. | PASS, 1/3 CPU |

Các ca hồi quy liên quan trong 60 ca xv6 gồm `forktest`, `preempt`, `exitwait`,
`reparent`, `reparent2`, `twochildren`, `forkfork`, `forkforkfork`, `sharedfd`,
`pipe1`, `killstatus`, `openiput`, `exitiput`, `sbrkfail` và các ca file/memory.
`usertests` cũng kiểm tra số trang rảnh trước/sau suite.

## Review và giới hạn

Đã rà lại toàn bộ diff: vị trí tăng/reset bộ đếm, thứ tự `wait_lock/p->lock`,
không giữ hai khóa process cùng lúc, phạm vi mảng, kiểm tra căn chỉnh con trỏ,
mọi nhánh nhả khóa và các chỗ ghi `ofile[]`. Không sửa `sched()/yield()/fork()`
hoặc `swtch.S`, không tạo `struct sysinfo` trên kernel stack.

Những phần chưa kiểm chứng trong phạm vi này:

- Chưa tích hợp nhánh của Vũ/Thi hoặc chạy `user/sysinfotest.c` qua syscall mới;
  chưa xác nhận buffer/serialization, xử lý `-1` từ collector và kiểm tra địa chỉ
  user của `sys_sysinfo()`. Kiểm thử `MAXVA` ở đây thuộc `sys_pipe`, không được
  tính là kiểm thử con trỏ user của syscall `sysinfo`.
- Chưa chạy sáu ca `slowtests` ngoài `usertests -q`, stress dài hạn, sanitizer,
  hoặc fault injection làm `kalloc()` thất bại ngay trong `allocproc()`. Đã có
  kiểm tra hết `NPROC` và ca memory/fork lỗi trong suite gốc.
- `reparent/reparent2` đã chạy hồi quy, nhưng collector chưa được đo đồng thời
  riêng với một workload reparent liên tục. Snapshot hiện không nguyên tử toàn
  hệ thống, đặc biệt giữa phần file và process.
- `ftable` được tham chiếu với layout global hiện tại; nếu thành viên khác đổi
  thành `static` hoặc đổi layout thì cần thống nhất helper/API trước khi ghép.
- `uint64` có thể wrap sau 2^64 dispatch; không thực nghiệm đến ngưỡng đó.

Không phát hiện lỗi chức năng trong phạm vi đã kiểm tra. Những kết quả trên
không thay thế kiểm thử tích hợp toàn bộ syscall và review của thành viên khác
theo `docs/review-pr.md`.
