# Kết quả sửa và review lại PR #3 — 07/11/2026

**Trạng thái: sẵn sàng review lại.** Tài liệu ghi kết quả kiểm thử bản sửa;
việc commit/push không đồng nghĩa merge, đăng review, cập nhật PR description
hoặc đóng thread.

Nhánh local: `file-management`, worktree `.worktrees/pr3-file-management`.
Commit nền vẫn là `ceb100d8c4948422b0c1ed269aca16a047dcca20` của PR #3.
Nhánh `feature/process-info` tại thư mục gốc vẫn ở `aee410e` và không có
thay đổi mã nguồn tracked. Các file nguồn, test và tài liệu dưới đây là
phần bàn giao trên nhánh File Management.

## Lỗi và nguyên nhân gốc

| Mục review | Sửa và căn cứ |
| --- | --- |
| M1: slot_of bị truy cập bằng hiệu con trỏ chưa xác thực | Collector dùng file_index_of, kiểm tra chỉ số trước khi truy cập mảng. NULL được bỏ qua; con trỏ bất thường trả -1, xóa phần file đã điền và nhả khóa. Helper dùng cùng implementation với PR #4. |
| M2: các khóa khác nhau không bảo vệ được ofile[] | Bỏ ofile_lock do PR #3 thêm, dùng p->lock cho reader/writer, kể cả rollback pipe. Fork giữ np->lock cho bảng con; không thêm khóa parent. Close/exit/rollback nhả khóa trước fileclose. Protocol trùng với PR #4. |
| Collector process tạm có thể xuất file_index=-1 và giữ dữ liệu cũ | Phục hồi kiểm tra lỗi ánh xạ/xóa phần process theo bản sửa đã có ở PR #2, khi tháo ofile_lock. Không thay collector process bằng một thiết kế mới hoặc sửa scheduler. |
| P3: scheduling_count bị cắt về int lúc in | Dùng lại putu(uint64) của PR #2, không sửa thư viện printf hoặc kiểu trường chung. |
| Thiếu hồi quy descriptor biến động | Filetest có 4 worker dup/close, open/close, pipe, rollback đồng thời với 10.000 snapshot; kiểm tra bounds, chỉ số hợp lệ, PID không trùng, phần dư và cleanup. |
| Patch lưu trong PR khác code/test thực tế | Bỏ ba snapshot patch cũ/test-only. Mã nguồn đang chạy là căn cứ; patch nộp cuối thuộc bước tích hợp của Sang. |
| Thiếu bằng chứng bàn giao phần File Management trong PR | Thêm báo cáo file-management-report.md, runner tái lập và log thực tế. Không kết luận các báo cáo bên ngoài PR trước đây chưa tồn tại. |

Lỗi syscall bỏ qua mã lỗi collector và cơ chế đọc ofile hai lần đã được
head PR #3 sửa trước lượt này; không sửa máy móc lại các đường đã đúng.
Syscall hiện tại vẫn kiểm tra mã lỗi, bỏ copyout khi lỗi và nhả sleeplock.

## File thay đổi

- Kernel: `kernel/file.c`, `kernel/proc.c`, `kernel/proc.h`, `kernel/sysfile.c`.
- User: `user/filetest.c`, `user/sysinfotest.c`.
- Test mới: `tests/file_info_native.c`, `tests/sysinfo_print_native.c`,
  `tests/file_info_kernel.c`, `tests/run_file_info.py`.
- Tài liệu mới: `docs/file-management-report.md`, tài liệu kết quả này.
- `.gitignore`: loại log/build kết quả và cache Python khỏi source.
- Bỏ `kernel/24120183_file_part_v2.patch`,
  `user/24120183_file_on_sysinfo_v2.patch`,
  `user/test_only_sysinfo_wiring.patch`.

Không thay `kernel/sysinfo.h`, `kernel/defs.h`, syscall ABI, Makefile,
thuật toán scheduler hoặc bộ đếm scheduling. Hai file proc chỉ đổi phần
khóa/descriptor và xử lý kết quả của collector tạm liên quan đến file.

## PASS/FAIL thực tế

Môi trường: WSL Ubuntu 24.04, riscv64-unknown-elf-gcc 13.2.0, QEMU 8.2.2.
Build dùng `make -j4 TOOLPREFIX=riscv64-unknown-elf- kernel/kernel fs.img`.
Các bản build nằm trong thư mục tạm; production không gắn probe.

| Kiểm tra | 1 CPU | 3 CPU |
| --- | --- | --- |
| Bản sửa PR #3: build | PASS | Cùng bản build |
| filetest, gồm 4 worker/10.000 snapshot và rollback | PASS | PASS |
| sysinfotest | 84/84 PASS | 84/84 PASS |
| forktest | PASS | PASS |
| usertests -q | 60 ca PASS | 60 ca PASS |
| Ghép module PR #4: filetest/sysinfotest/forktest | PASS, 84/84 | PASS, 84/84 |
| Ghép module PR #4: usertests -q | 60 ca PASS | 60 ca PASS |
| Probe kernel: bản sửa và bản ghép PR #4 | Cả hai PASS | Cả hai PASS |
| Ghép nguyên module PR #2 và PR #4: build/filetest/forktest | PASS | PASS |
| Ghép ba module: sysinfotest của PR #2 | 86/86 PASS | 86/86 PASS |
| Ghép ba module: preempt/sharedfd/pipe1/exitwait/reparent | Cả 5 PASS | Cả 5 PASS |

Hai probe native dưới ASan/UBSan cũng PASS: collector thật với mọi offset
lệch, ngoài bảng, guard, NFILE/NPROC/NOFILE, PID/ref và bảo toàn phần process;
formatter thật với 0, 2^31, 2^32, UINT64_MAX. Không có báo cáo sanitizer
hoặc kernel panic trong các lượt mới. Các test chủ động gây lỗi user page
trong usertests in usertrap theo thiết kế rồi PASS, không phải kernel panic.

Không có bài kiểm thử chức năng FAIL ở bản sửa cuối.

Refs tích hợp đã kiểm tra: PR #2 `d25f259badff6a9e14fcdf22ae46d85fd2a9bbbe`,
PR #4 `aee410ecf54ce6c8d7ca192929ef64d5a57c2aaf`.
Bản ghép lấy nguyên proc.c/proc.h/sysfile.c từ PR #4; lượt ba module còn
lấy nguyên sysproc.c/sysinfotest.c từ PR #2. Không tạo merge commit.
Filetest của Vũ vẫn chạy 10.000 snapshot; sysinfotest nguyên PR #2 có thêm
regression dup/close 2.000 snapshot. Không dùng test bắt ref bằng tổng FD
khi hệ thống đang biến động.

Log đã lưu tại:

- [Native collector](../test-results/file-info/file_info_native.log),
  [native formatter](../test-results/file-info/sysinfo_print_native.log).
- [PR #3: 1 CPU](../test-results/file-info/qemu-fixed-cpu1.log),
  [3 CPU](../test-results/file-info/qemu-fixed-cpu3.log).
- [Ghép PR #4: 1 CPU](../test-results/file-info/qemu-integrated-cpu1.log),
  [3 CPU](../test-results/file-info/qemu-integrated-cpu3.log).
- [Probe kernel: 1 CPU](../test-results/file-info/qemu-fixed-probe-cpu1.log),
  [3 CPU](../test-results/file-info/qemu-fixed-probe-cpu3.log).
- [Ghép ba module: 1 CPU](../test-results/file-info-triple/qemu-integrated-cpu1.log),
  [3 CPU](../test-results/file-info-triple/qemu-integrated-cpu3.log).

Log là artifact local được gitignore; báo cáo kết quả và runner là file bàn
giao. Có thể tái lập bằng các lệnh trong file-management-report.md.

## Review lại toàn bộ diff và giới hạn

Đã đọc lại diff sửa lỗi, toàn bộ diff PR so với main và các file mới.
Đã rà mọi chỗ ghi ofile[] trong kernel; cùng protocol với PR #4. Đã kiểm
tra packing/count, PID, ref, việc tái sử dụng slot, đường lỗi, thứ tự khóa,
stack/buffer và ABI. Không thấy lỗi chức năng mới trong phạm vi đã kiểm tra.
`git diff --check` của lượt sửa PASS. Diff toàn PR so với main còn các
khoảng trắng cũ trong syscall.h/sysproc.c và comment sysinfotest; chúng
không là lỗi chức năng và không thay đổi logic ngoài phạm vi File Management.

Snapshot vẫn không nguyên tử toàn hệ thống; ref/PID/FD có thể khác thời
điểm. Tài liệu mô tả rõ, không hứa một snapshot nguyên tử hoặc identity
ổn định qua việc tái sử dụng file_index. Không có yêu cầu đổi ABI để làm
snapshot nguyên tử trong đề/quy ước đã đối chiếu.

Linker vẫn cảnh báo RWX ở initcode/kernel/forktest; không mở rộng PR sang
linker. Chưa chạy slowtests hoặc stress dài hạn. Các nhánh có thể còn
conflict văn bản khi tích hợp; kết quả module cho thấy lựa chọn code tương
thích, không chứng minh Git tự merge sạch. Khi chốt tích hợp dùng collector
process chính thức của Sang và cập nhật PR description theo kết quả mới.

PR cần được review lại trên commit chứa bản sửa. Kết quả kiểm thử không
thay thế Approve của thành viên khác; không tự thay đổi review hoặc thread.
