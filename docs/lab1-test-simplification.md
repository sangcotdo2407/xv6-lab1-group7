# Lab 1 — Rút gọn test và bỏ comment

Ngày 07/11/2026. Code được kiểm thử: `57020f038f1f0049ddc84f5fdd31bf60e9576882`.
Nhánh `integration/lab1`, PR tích hợp [#5](https://github.com/sangcotdo2407/xv6-lab1-group7/pull/5).
Backup trước sửa: `backup/lab1-before-test-simplification`, trỏ đến `f034574`.

## Phạm vi và số dòng

| Chương trình | Trước | Sau | Trách nhiệm hiện tại |
| --- | ---: | ---: | --- |
| user/sysinfotest.c | 619 | 325 | Thống kê cơ bản, file/process lifecycle, scheduling, giới hạn, user pointer và syscall đồng thời. |
| user/filetest.c | 472 | 130 | Descriptor lỗi, toàn bộ rollback pipe và stress file/descriptor. |
| Tổng | 1.091 | 455 | Giảm 636 dòng, khoảng 58%. |

Không ép mã thành một khối dài hoặc chuyển nội dung sang header để giấu số
dòng. Hai chương trình vẫn độc lập và nằm trong UPROGS. Gộp khẳng định có
cùng setup/snapshot; gom ca trùng của filetest vào sysinfotest; dùng cùng một
validator trong mỗi chương trình cho mọi snapshot.

Bỏ comment trong toàn bộ mã nguồn kernel/user/mkfs, assembly, linker script,
Makefile, script Python/Perl và cấu hình editor tracked. Giữ shebang, chỉ thị
preprocessor và chuỗi ký tự thực thi. LICENSE giữ nguyên; tài liệu Markdown
và log bằng chứng không phải comment mã nguồn nên được giữ lại.

Đã so sánh 65 file C/header/assembly/linker chỉ thay comment: token mã lệnh
trước/sau giống nhau. Ba Python runner có AST thực thi giống nhau khi bỏ
docstring. Unit test cho runner được sửa để không phụ thuộc một câu comment
trong proc.c. user/usys.pl không sinh dòng comment đầu file usys.S nữa;
nội dung stub lệnh và khai báo syscall giữ nguyên.

Việc bỏ comment làm diff lan sang các file xv6 nền; các thay đổi đó không
thay hành vi kernel. Phần refactor có logic thực thi nằm ở hai user test và
unit test vị trí hook. Header sysinfo chỉ mất comment, ABI không đổi.

## Đối chiếu độ bao phủ

| Nhóm trước rút gọn | Vị trí hiện tại |
| --- | --- |
| Basic, repeated calls, init/parent/state, console aliases/device/mode/PID | sysinfotest test_files, 50 lần gọi ổn định; dump đầy đủ file và process, uint64 không bị cắt. |
| open/close, cùng path hai lần, RDONLY/WRONLY/RDWR, type/ref/PID | test_files; kiểm object riêng và số file mong đợi. |
| dup, nhiều FD một object, ref khác số PID, đóng từng alias và cuối cùng | test_files; ref=3 rồi 2, một PID và cleanup. |
| fork, pipe type/mode, parent PID, FD kế thừa, sleeping, scheduling | test_processes; child chờ pipe và tự gọi sysinfo. |
| fork kết hợp dup | test_processes; hai FD đầu đọc × hai process = ref 4, chỉ hai PID. |
| exit/zombie/wait, giảm ref, xóa process, slot/PID mới | test_processes; kernel selftest vẫn kiểm reset counter và reuse chi tiết. |
| scheduling tăng, không giảm, in 64 bit | test_scheduling; native formatter và kernel 32 dispatch giữ nguyên. |
| NOFILE/NPROC/NFILE, open/fork thất bại, khôi phục tài nguyên | test_limits; dùng gate và chờ worker ngủ trước khi so ref/PID giữa các pha. |
| NULL/text chỉ đọc, MAXVA/kernel/trapframe/trampoline/guard, địa chỉ tràn/sai | test_pointers; text/NULL chạy trong process con, con vẫn thoát đúng. |
| Copyout nhiều trang, lệch byte, vừa khít, tràn một byte, đích unmapped, canary | test_pointers, chỉ dùng heap tạm cho trường hợp có thể copy một phần. |
| Phần tử dư bằng 0, giới hạn mảng, ID duy nhất và FD tăng dần | valid_snapshot/snapshot_ok áp dụng cho mọi snapshot, kể cả stress. |
| Caller đồng thời, buffer kernel chung | test_concurrent(0): bốn process × 40 syscall. |
| Race dup/close với collector | test_concurrent(1): bốn worker + 2.000 snapshot; kiểm status và cleanup. |
| Pipe rollback fdalloc thứ nhất/thứ hai, copyout thứ nhất/thứ hai | filetest test_errors; kiểm cả số object lẫn số FD để phát hiện rò. |
| FD âm/vượt NOFILE, open path không tồn tại | filetest test_errors. |
| Open/close, dup/close, pipe và rollback cùng lúc | filetest test_churn: bốn worker + 10.000 snapshot; kiểm PID, mode, bounds, zero tails và cleanup. |
| Con trỏ kernel bất thường, fault collector và hồi phục, stressfs dữ liệu | Giữ các probe/native/extra runner trong tests và chạy lại. |

Số CHECK giảm do gom các khẳng định tương đương; không lấy số CHECK làm số
lượng tình huống. Mỗi snapshot còn kiểm các bất biến bằng vòng lặp; các
require cho setup, đồng bộ và recovery vẫn làm test FAIL nếu không đạt.
Không đòi ref bằng tổng FD giữa các pha khi worker đang thay đổi descriptor.

## Kết quả kiểm thử sau rút gọn

| Kiểm tra | 1 CPU | 3 CPU |
| --- | --- | --- |
| Clean build, make qemu | PASS | PASS |
| sysinfotest | 42/42 PASS | 42/42 PASS |
| filetest | 12 check PASS, 10.000 snapshot | 12 check PASS, 10.000 snapshot |
| forktest | PASS | PASS |
| usertests -q | 60 ca PASS | 60 ca PASS |
| Kernel process/scheduling selftest | PASS, gồm 32 dispatch chính xác | PASS, gồm 32 dispatch chính xác |
| Kernel file probe | PASS | PASS |
| Fault từng collector, không copyout, 100 lần phục hồi | PASS | PASS |
| stressfs + kiểm nội dung/EOF | 5 file × 10.240 byte đúng | 5 file × 10.240 byte đúng |

Native file helper/collector/metadata và formatter uint64 đạt ASan/UBSan;
4/4 unit test runner đạt. git diff --check đạt. Không có kernel panic.
Các cảnh báo linker RWX có sẵn và giới hạn snapshot không nguyên tử giữ
nguyên như [báo cáo tích hợp](lab1-integration-review.md). Chưa chạy slowtests
hoặc stress dài hạn; không nhận PASS cho những mục này.

Lệnh đã chạy từ Linux/WSL, với output ở test-results/lab1-simplified:

```sh
python3 tests/run_file_info.py --cpus 1 3 --quick-suite --output test-results/lab1-simplified/final
python3 tests/run_process_info.py --cpus 1 3 --quick-suite --output test-results/lab1-simplified/process
python3 tests/run_file_info.py --kernel-probe-only --cpus 1 3 --output test-results/lab1-simplified/file-probe
python3 tests/run_lab1_extra.py --cpus 1 3 --output test-results/lab1-simplified/extra
python3 -m unittest discover -s tests -p test_process_info_runner.py -v
```

[Log hiện tại và manifest](lab1-evidence/simplified/manifest.json) nằm trong
lab1-evidence/simplified/. Log gốc giữ ở test-results; bản trong Git chỉ chuẩn
hóa LF và bỏ whitespace cuối dòng. Manifest chứa hash cả bản thô và bản lưu,
hash nguồn theo LF, commit code và bằng chứng token không thay đổi.

Đủ điều kiện kỹ thuật để tiếp tục review PR #5. Không tự Approve hoặc merge
main. Các thông số 102/102 và 49 check ở báo cáo cũ chỉ còn là lịch sử của
bản trước rút gọn, không phải kết quả của code hiện tại.
