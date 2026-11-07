# Lab 1 — Kết quả tích hợp cuối

Ngày kiểm tra: 07/11/2026, múi giờ Việt Nam. Repository:
`sangcotdo2407/xv6-lab1-group7`. Báo cáo này mô tả cây tích hợp cuối;
các báo cáo review theo từng PR được giữ làm lịch sử, không thay thế kết quả dưới đây.

## Safeguard và nguồn đối chiếu

Trước fetch, checkout gốc ở `feature/process-info`, HEAD `aee410e`, không có
thay đổi tracked, staged hoặc commit local chưa push. Các file untracked thuộc
`.vscode/` và `.worktrees/` (worktree, script và bằng chứng cũ) được giữ nguyên.
Không stash, reset, clean, force push hoặc sửa nhánh làm việc của thành viên.

Sau `git fetch origin`, main là `1e90d3a00993aca076ed3e46c38356c2217b4d63`.
Tạo bốn refs `backup/lab1-20261107-main`, `backup/lab1-20261107-pr2`,
`backup/lab1-20261107-pr3`, `backup/lab1-20261107-pr4` trước khi merge.
Nhánh `integration/lab1` được tạo từ origin/main trong worktree riêng
`.worktrees/lab1-integration`; main local và remote không bị cập nhật.

| PR | Head chính xác trước tích hợp | Head branch → base | Trạng thái GitHub khi bắt đầu |
| --- | --- | --- | --- |
| [#2](https://github.com/sangcotdo2407/xv6-lab1-group7/pull/2) | `71c6a32c9e43aecb9b4bfc907b933828e4470276` | sysinfo → main | OPEN, chưa merge, không draft |
| [#3](https://github.com/sangcotdo2407/xv6-lab1-group7/pull/3) | `45b285aac973e3fd4c8a61914ef7f2c9bb3677ae` | file-management → main | OPEN, chưa merge, không draft |
| [#4](https://github.com/sangcotdo2407/xv6-lab1-group7/pull/4) | `aee410ecf54ce6c8d7ca192929ef64d5a57c2aaf` | feature/process-info → main | OPEN, chưa merge, không draft |

Metadata PR ban đầu trả `base_sha` lần lượt `7a00bda`, `46d7585`, `1e90d3a`;
đây không phải ba nền để tích hợp. Nền duy nhất của worktree là origin/main
`1e90d3a` đã fetch. Lịch sử review chính thức còn CHANGES_REQUESTED ở #2/#3
trên commit cũ và chưa có review ở #4; không tự suy ra đã có Approve trên head mới.

PR #1 được merge tại `7a00bda`, rồi revert tại `46d7585`. Đã kiểm tra cả hai
diff: không khôi phục `kernel/24120183_file_part.patch`, `kernel/filetest.c`
hay các snapshot patch cũ. `user/filetest.c` hiện tại là test được phát triển
và sửa trong PR #3. Không revert lại commit revert của PR #1.

Đã đọc `Lab1_File_Process.pdf` (đặc biệt trang 2, mục 4.1/4.2),
`Lab1_Nhom7_PhanCong_ThongNhat_Sysinfo.docx` được cung cấp, cùng
`docs/sysinfo-conventions.md`, `docs/review-pr.md` và mã nguồn liên quan.
Tên `Phan_cong_Lab1_Nhom7.docx` trong yêu cầu không trùng tên file đính kèm;
đối chiếu bằng DOCX thực tế nêu trên. Nội dung tài liệu được dùng làm căn cứ
kỹ thuật; các bước nộp bài trong đó không được coi là lệnh xuất bản/nộp bài.

## Thứ tự merge và giải quyết conflict

| Bước | Commit tích hợp | Conflict và quyết định |
| --- | --- | --- |
| PR #2 | `85afc00ddb9b3e4bd8bf6181a68eee4570f51d02` | Merge sạch; thêm syscall và bộ test đã sửa. |
| PR #3 | `87a86ba9d4781ac6a129f980a00d352b0e83b900` | file.c, proc.c, sysfile.c, sysproc.c, sysinfotest.c. Dùng collector file chính thức có xóa phần dư/xử lý lỗi; giữ khóa descriptor và bộ test đầy đủ của #2; syscall dùng chuỗi if/else kiểm tra lỗi rõ ràng. Các đoạn process tương đương được giữ tạm trước bước #4. |
| PR #4 | `78f6148ca8d2ee674cd44c69187219de085d6ed2` | file.c, proc.c, proc.h, sysfile.c. Giữ collector file; dùng collector process chính thức của #4 và reset counter ở alloc/free; chỉ một helper file_index_of. exit đọc/xóa cùng dưới p->lock rồi đóng sau khi nhả khóa. |
| Sửa tích hợp | `6c57aac` | Đồng bộ metadata file, khởi tạo mode sạch, khôi phục phong cách xv6 và thêm kiểm thử. |

Đã xem diff/lịch sử trước từng merge; giải quyết dựa trên chức năng từng
đoạn, không dùng chiến lược tự chọn toàn bộ ours/theirs. Cả ba head PR là
tổ tiên của cây tích hợp; không viết lại lịch sử các nhánh thành viên.

## Phát hiện, nguyên nhân và sửa

| Mức | Phát hiện | Nguyên nhân và cách xử lý |
| --- | --- | --- |
| P2, correctness/concurrency | Snapshot có thể trộn type/mode lúc khởi tạo file. | Collector đọc dưới ftable.lock nhưng sys_open/pipealloc ghi ngoài khóa. Thêm helper nội bộ filesetinfo, công bố ba trường cùng dưới ftable.lock; inode/pipe được chuẩn bị riêng, không sleep dưới spinlock. |
| P2, dữ liệu cũ | File vừa cấp phát có thể giữ mode của lần dùng trước. | fileclose chỉ xóa type; filealloc trước đây chỉ đặt ref. Reset type/readable/writable trước ref=1. Regression native thực sự FAIL trước sửa, PASS sau sửa. |
| Tích hợp | Collector tạm, helper và protocol descriptor bị chồng chéo giữa các PR. | Hợp nhất đúng một implementation mỗi hàm, dùng phần file của #3, process của #4, syscall/test của #2. Giữ mọi writer ofile[] dưới p->lock, kể cả rollback pipe và fork vốn giữ np->lock. |
| P3, khả năng đọc | Format lại toàn bộ file.c và comment quá dài gây khó xem diff. | Khôi phục phần nền về phong cách xv6, rút gọn comment collector; giữ comment khóa, reference và copyout cần thiết. |

Các lỗi từng review trước đó được giữ nguyên bản sửa: kiểm tra miền/căn chỉnh
file_index_of, đọc descriptor một lần dưới khóa, trả lỗi collector, xóa phần dư,
in uint64 đầy đủ, đồng bộ setup test và tránh dùng stack sống làm đích copyout lỗi.

Không đổi `kernel/sysinfo.h`, kiểu trường, tên/chữ ký collect_*, số syscall 22
hoặc user API. `filesetinfo` chỉ là helper nội bộ mới khai báo trong defs.h;
không thêm lock/field vào struct file, không đổi scheduler/sched/yield/swtch.
Không sửa vm.c: copyout nền đã kiểm tra PTE_V/PTE_U/PTE_W và MAXVA.

## Review khóa và bộ nhớ

- Buffer tĩnh kinfo 38.248 byte, lớn hơn stack kernel 4.096 byte. Sleeplock
  tuần tự hóa toàn bộ thu thập/copyout; mọi đường trả lỗi đều nhả khóa.
- ftable.lock bảo vệ ref và công bố type/mode. Collector nhả nó trước khi
  lấy từng p->lock. filesetinfo không lấy khóa process, inode hay pipe.
- wait_lock đi trước p->lock; process collector nhả khóa con trước khi lấy
  khóa cha, giữ wait_lock để ổn định parentage/lifetime. Không giữ hai khóa proc.
- close/exit/rollback tháo descriptor trước fileclose; fileclose nhả ftable.lock
  trước pipeclose/iput/begin_op. Không sleep hoặc copyout dưới khóa bảng spinlock.
- Mỗi bảng chỉ duyệt NFILE/NPROC/NOFILE ô. Helper không dereference con trỏ
  bất thường. Hai collector chỉ ghi phần của mình; lỗi không trả danh sách dở dang.
- argaddr chỉ lấy địa chỉ; copyout xác thực mapping/quyền theo từng trang.
  Khi copyout lỗi giữa chừng, user buffer có thể đã bị ghi một phần; caller
  phải bỏ kết quả khi nhận -1. Lỗi collector bỏ qua copyout hoàn toàn.

## Kiểm thử thực tế

Môi trường: WSL Ubuntu 24.04, riscv64-unknown-elf-gcc 13.2.0,
QEMU 8.2.2, 128 MiB RAM, CPUS=1 và CPUS=3. Build bật -Wall -Werror.
Mỗi lượt QEMU dùng filesystem image sạch. Chỉ bản sao tạm chứa fault hook,
kernel selftest hoặc chương trình stresscheck; production không chứa các hook đó.

| Kiểm tra trên cây cuối | 1 CPU | 3 CPU | Bằng chứng |
| --- | --- | --- | --- |
| Clean build + make qemu | PASS | PASS | extra/build-production.log, qemu-production-*.log |
| filetest | 49 check PASS, ALL PASSED | 49 check PASS, ALL PASSED | final/qemu-fixed-*.log |
| sysinfotest | 102/102 PASS | 102/102 PASS | final và extra production logs |
| forktest | PASS | PASS | final và process logs |
| usertests -q | 60 ca, ALL TESTS PASSED | 60 ca, ALL TESTS PASSED | final và process logs |
| File kernel probe | PASS | PASS | file-probe logs |
| Process/scheduling kernel selftest | PASS | PASS | process logs |
| Fault từng collector + recovery | PASS | PASS | extra/qemu-fault-*.log |
| stressfs + kiểm nội dung/EOF | 5 file × 10.240 byte đúng | 5 file × 10.240 byte đúng | extra production logs |
| Native file helper/collector + metadata | ASan/UBSan PASS | Không phụ thuộc CPU QEMU | final/file_info_native.log |
| Native uint64 formatter | ASan/UBSan PASS | Không phụ thuộc CPU QEMU | final/sysinfo_print_native.log |
| Runner instrumentation | 4/4 unittest PASS | Không phụ thuộc CPU QEMU | unittest trong phiên kiểm thử |
| git diff --check origin/main | PASS | Không phụ thuộc CPU | Kiểm tra cuối trước commit/push |

Các test kiểm tra giá trị mong đợi, không chỉ việc kernel còn sống:

- open cùng path tạo object khác; dup/fork dùng cùng index, ref thay đổi theo
  số FD, PID không trùng. close cuối/exit/wait khôi phục số lượng ban đầu.
- Đủ NOFILE/NPROC/NFILE, đầy một descriptor table; pipe rollback ở fdalloc
  thứ nhất/thứ hai và copyout fd thứ nhất/thứ hai không rò file/FD.
- sysinfotest: PID cha, RUNNING/SLEEPING/ZOMBIE/UNUSED, bộ đếm tăng, con trỏ
  MAXVA/kernel-only/read-only/guard/unmapped, buffer nhiều trang/lệch byte,
  canary và phần dư bằng 0; 4 caller × 40 lần và 2.000 snapshot khi dup/close.
- filetest: 10.000 snapshot với 4 worker dup/close, open/close, pipe, rollback.
- Kernel process selftest: 32 vòng yield với số dispatch chính xác; uint64,
  reset/tái sử dụng slot; alias FD, exit thưa và reference cleanup chính xác;
  fork/sleep/zombie/wait, 200 vòng churn và ít nhất 500 snapshot.
- Kernel file probe: file thật, mọi offset sai căn chỉnh, error cleanup và
  canary. Native còn kiểm mọi index, bảng đầy/rỗng và bảo toàn phần process.
- Fault injection: collector file trả -1 một lần, rồi collector process trả
  -1 một lần; user buffer vẫn nguyên 0xA5, tiếp theo 100 syscall hợp lệ thành công.
- stressfs được kiểm lại bằng stresscheck: đọc đủ 20 block × 512 byte, từng
  byte là 'a', sau đó EOF và cleanup, cho cả 5 file.

Lệnh chạy từ nhánh tích hợp trong Linux/WSL:

```sh
python3 tests/run_file_info.py --cpus 1 3 --quick-suite --output test-results/lab1-integration/final
python3 tests/run_file_info.py --kernel-probe-only --cpus 1 3 --output test-results/lab1-integration/file-probe
python3 tests/run_process_info.py --cpus 1 3 --quick-suite --output test-results/lab1-integration/process
python3 tests/run_lab1_extra.py --cpus 1 3 --output test-results/lab1-integration/extra
python3 -m unittest discover -s tests -p test_process_info_runner.py -v
git diff --check origin/main
```

run_lab1_extra thực sự chạy `make clean`, `make -j4 kernel/kernel fs.img`,
`make qemu CPUS=1` và `make qemu CPUS=3`. Các runner còn lại dùng dòng lệnh
QEMU tương đương trong bản sao build. Log trước sửa có FAIL ở regression mode
được lưu riêng; không tính lần FAIL đó là kết quả của cây cuối.
[Bằng chứng được commit](lab1-evidence/README.md) gồm log QEMU, native, build,
ELF và manifest có SHA-256. Log gốc đầy đủ trong test-results/lab1-integration
của workspace; không stage binary/build artifact.

## Đối chiếu từng yêu cầu 4.1 và 4.2

| Yêu cầu PDF | Mã nguồn / giải thích / kiểm chứng | Kết quả |
| --- | --- | --- |
| 4.1 ofile[] và ftable.file[] | file-management-report, file.c, proc.h; open/dup/fork/ref/PID test | Đáp ứng |
| 4.1 luồng open | file-management-report và lab1-viva; sys_open/fdalloc/filealloc | Đáp ứng |
| 4.1 so sánh open/dup/fork | Bảng trong file-management-report và lab1-viva; kiểm ref/index thực tế | Đáp ứng |
| 4.1 pid/state/context | process-info-report và lab1-viva; phân biệt context/trapframe | Đáp ứng |
| 4.1 luồng fork | process-info-report; allocproc/uvmcopy/filedup/parent/RUNNABLE | Đáp ứng |
| 4.1 scheduler/sched/yield | process-info-report; kiểm 32 dispatch, regression preemption | Đáp ứng |
| 4.2 số file, type/mode/ref/PID | collect_file_info/filesetinfo; 49 filetest check và sysinfotest | Đáp ứng |
| 4.2 process != UNUSED, pid/state/parent/counter/files | collect_process_info và scheduler; kernel/user tests | Đáp ứng |
| 4.2 sysinfo(struct sysinfo *) và header | syscall 22, stub/prototype, ABI giữ nguyên, argaddr/copyout | Đáp ứng |
| 4.2 user/sysinfotest.c, nhiều tình huống | 12 nhóm, 102 check, chạy nhiều lần trên 1/3 CPU | Đáp ứng |

## File thay đổi và re-review

Production: Makefile; kernel/defs.h, file.c, file.h, pipe.c, proc.c, proc.h,
syscall.c, syscall.h, sysfile.c, sysproc.c; user/user.h, usys.pl,
sysinfotest.c, filetest.c. sysinfo.h đã tồn tại trên main và giữ nguyên.

Kiểm thử: tests/file_info_kernel.c, file_info_native.c, process_info_kernel.c,
run_file_info.py, run_process_info.py, sysinfo_print_native.c,
test_process_info_runner.py, run_lab1_extra.py, stressfs_check.c, sysinfo_fault.c;
.gitignore chỉ bỏ qua test-results và Python cache.

Tài liệu: các báo cáo file/process/syscall của ba PR, báo cáo này,
lab1-viva.md và lab1-evidence/. Không có file patch cũ, file sinh tự động,
kernel/fs.img/binary, cấu hình editor hoặc log không liên quan trong diff.

Đã re-review toàn bộ diff so với origin/main: syscall wiring, ABI, mọi writer
descriptor/metadata, thứ tự khóa, error cleanup, ref, parent lifetime, counter,
giới hạn mảng và setup/cleanup test. Không phát hiện lỗi nghiêm trọng còn mở
trong phạm vi tích hợp. Các check bắt buộc chạy được đều đạt.

## Giới hạn và điều kiện merge

Snapshot nhất quán khi đọc từng hàng, không nguyên tử toàn hệ thống: các pha
file/PID/process khác thời điểm, slot file có thể tái sử dụng, ref không nhất
thiết bằng tổng FD giữa các pha khi có churn. File đang cấp phát có thể có
FD_NONE với mode 0. Đây là giới hạn được công bố, không phải yêu cầu phải đổi
toàn bộ thiết kế khóa. Không thêm retry vô hạn hoặc khóa toàn hệ thống.

Linker còn cảnh báo RWX ở initcode/kernel/forktest, đã có trong bản nền;
sysinfotest có text R E và data RW. Không có cảnh báo C mới hay kernel panic.
Các usertrap trong usertests là ca cố ý truy cập sai và kết thúc bằng OK.
Chưa chạy slowtests, grind vô hạn, stress dài hạn hoặc toolchain RISC-V khác;
không nhận PASS cho những mục này. Bộ đếm uint64 vẫn có wrap tự nhiên sau 2^64
dispatch; không đổi thành bộ đếm bão hòa.

Mã nguồn và phần khảo sát đáp ứng 4.1/4.2 với giới hạn nêu trên. Việc tạo
Report.pdf, patch nộp bài, Source.zip và nộp MOODLE không thuộc tác vụ tích hợp
PR này. Hướng dẫn vấn đáp cho cả ba thành viên ở [lab1-viva.md](lab1-viva.md).

Cây tích hợp đủ điều kiện kỹ thuật để reviewer xem xét merge. Vẫn cần ít nhất
một Approve hợp lệ từ thành viên khác theo docs/review-pr.md và xác nhận của
người dùng trước khi merge main. Phiên này không Approve, merge, bật auto-merge
hoặc đóng các PR gốc.
