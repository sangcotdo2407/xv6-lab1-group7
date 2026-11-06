# PR #2 — System Call Integration & Testing: review và kiểm thử

Ngày kiểm tra: 07/11/2026. Nhánh: `sysinfo`.
Head trước khi sửa: `d25f259badff6a9e14fcdf22ae46d85fd2a9bbbe`.
Đối chiếu toàn bộ diff với `origin/main` tại `1e90d3a`, lịch sử 23 commit,
formal review Request Changes và hai comment Conversation. Danh sách inline
review threads trả về rỗng. Không thay đổi trạng thái review hoặc gửi comment.

## Tài liệu và phạm vi

Đã đọc đề `Lab1_File_Process.pdf`, bản phân công được cung cấp dưới tên
`Lab1_Nhom7_PhanCong_ThongNhat_Sysinfo.docx`, `docs/sysinfo-conventions.md`
và `docs/review-pr.md` từ main. Tài liệu DOCX phân công Hồ Sỹ Thi phụ trách
đăng ký syscall, argaddr/copyout, chương trình kiểm thử và báo cáo tích hợp.
Đề yêu cầu sysinfo trả thông tin file và mọi process có state != UNUSED;
thiết kế ABI cụ thể là quy ước của nhóm.

Thực hiện trong worktree riêng `.worktrees/pr2-sysinfo`. Nhánh gốc
`feature/process-info` và các thư mục chưa tracked `.vscode/`, `.worktrees/`
được giữ nguyên. Không stash, reset, rebase hoặc thay đổi nhánh thành viên khác.

## Lỗi và xử lý

| Mức | Căn cứ tại head trước khi sửa | Nguyên nhân và cách khắc phục |
| --- | --- | --- |
| P1, phần còn lại của race cũ | Collector giữ p->lock nhưng fdalloc, close, hai đường rollback pipe và exit vẫn ghi ofile[] ngoài khóa. Exit còn giảm ref trước khi bỏ descriptor. | Dùng p->lock cho mọi writer này; exit lấy con trỏ và xóa descriptor dưới khóa, rồi nhả khóa trước fileclose. Fork đã giữ np->lock khi điền bảng con nên không thêm khóa cha. Ghi rõ protocol trong proc.h, cùng protocol PR #3/#4. |
| P2, kiểm thử con trỏ | t_badptr truyền top-8 khi chưa có heap riêng; đây là cuối trang stack đang dùng. copyout có thể ghi phần đầu rồi mới báo lỗi ở trang kế tiếp. | Cấp một trang heap riêng cho probe đi qua cuối mapping và thu hồi sau test. Thêm NULL/text chỉ đọc trong process con, guard page, trapframe và trampoline. |
| P2, kiểm thử dữ liệu | dump và các vòng lặp dùng count/type/state trả về để truy cập mảng trước khi kiểm tra đầy đủ. Test concurrent thiếu kiểm tra descriptor. | Thêm valid_snapshot, kiểm tra mọi count trước khi duyệt, miền enum/index/fd, PID và descriptor trùng. Dùng trong snap, concurrent và descriptor stress. |
| P2, lỗi setup/false PASS | t_concurrent không đếm fork thành công, không kiểm tra wait và có thể đọc st chưa khởi tạo. Nhiều pipe/fork/sbrk/read/write cũng chưa kiểm tra. Worker race có thể bỏ qua dup lỗi hoặc chết sớm. | Đếm worker đã tạo, khởi tạo và kiểm tra status, kiểm tra setup quan trọng, xác nhận đủ ready và trạng thái worker bị kill khi cleanup. |
| P2, đồng bộ test | sleep cố định không chứng minh con đã tới trạng thái cần đo; giới hạn file có thể được chụp khi con chưa mở xong. | Dùng ready pipe và chờ state có giới hạn; khi test đòi đối chiếu ref/PID, chờ worker ngủ sau khi hoàn tất thay đổi descriptor. Giữ hai endpoint ready trong parent qua phép đo NFILE để tránh làm mất slot vừa dùng để đạt giới hạn. |
| P2, thiếu hồi quy đường sửa | Các đường rollback pipe được thêm khóa nhưng suite chưa cố ý đi qua đầy đủ. | Thêm nhóm 12: lỗi copyout đầu, copyout fd0 thành công/fd1 lỗi ở trang kế tiếp, và chỉ còn một fd khiến fdalloc thứ hai lỗi; kiểm tra không rò file/fd. |
| P3 | git diff --check báo whitespace trong syscall.h, sysproc.c và sysinfotest.c. | Dọn whitespace trong phạm vi PR. |

Không tái hiện lại kết quả file_index ngoài giới hạn của review tại `1700d09`
trên head `d25f259`: head này đã đọc descriptor một lần và kiểm tra helper.
Việc suite cũ đạt 86/86 không chứng minh writer đã tuân thủ khóa; sửa khóa
dựa trên đối chiếu reader/writer trong toàn bộ mã nguồn.

Các mục đã được tác giả sửa trước phiên này, được giữ nguyên và kiểm chứng:

- `file_index_of()` dùng địa chỉ số nguyên, kiểm tra miền/căn chỉnh; không
  dereference con trỏ. Collector kiểm tra lỗi trước khi dùng index.
- `sys_sysinfo()` kiểm tra cả hai collector, short-circuit trước copyout và
  nhả sleeplock ở một điểm chung.
- `putu(uint64)` đã thay phép ép int khi in scheduling_count.
- Regression dup/close 4 worker, 2.000 snapshot đã tồn tại; phiên này củng cố
  xác thực dữ liệu, ready và kiểm tra worker/cleanup.

## Thiết kế tích hợp được review lại

SYS_sysinfo=22 khớp handler/dispatch, user prototype, usys.pl và UPROGS.
argaddr trả void theo phiên bản này; địa chỉ user chỉ được đưa qua copyout.
struct sysinfo là 38.248 byte, nằm trong bộ đệm tĩnh có sleeplock, không nằm
trên kernel stack. Bộ đệm được memset toàn bộ trước khi gọi collector,
bao gồm padding và phần mảng chưa dùng.

Không giữ ftable.lock hoặc p->lock khi copyout. Collector process giữ thứ tự
wait_lock -> p->lock. File collector nhả ftable.lock trước khi lấy p->lock;
helper ánh xạ index không lấy khóa. Các writer mới nhả p->lock trước
fileclose, không thêm khóa bao quanh đường có thể sleep. Không thay đổi
scheduler/sched/yield hoặc vị trí tăng scheduling_count.

Không đổi `kernel/sysinfo.h`, `kernel/defs.h`, chữ ký collector hay ý nghĩa
bộ đếm. Không nhập implementation chính thức của PR #3/#4 vào nhánh này.
Những sửa đổi proc/sysfile chỉ phục vụ protocol đồng bộ bắt buộc của sysinfo.

## Kết quả thực tế

Môi trường: WSL Ubuntu-24.04, GCC `riscv64-unknown-elf` 13.2.0,
QEMU 8.2.2. Mỗi bản được xuất sang thư mục /tmp mới, build bằng
`make -j4 kernel/kernel fs.img`, toolchain được Makefile tự nhận diện.

| Bản / kiểm tra | 1 CPU | 3 CPU |
| --- | --- | --- |
| Head gốc: build | PASS | Cùng bản build |
| Head gốc: sysinfotest | 86/86 PASS | 86/86 PASS |
| Head gốc: forktest | PASS | PASS |
| Head gốc: usertests -q | 60 ca PASS | 60 ca PASS |
| Bản cuối: build | PASS | Cùng bản build |
| Bản cuối: sysinfotest | 102/102 PASS | 102/102 PASS |
| Bản cuối: forktest | PASS | PASS |
| Bản cuối: usertests -q | 60 ca PASS | 60 ca PASS |
| Chèn lỗi collector trong bản build tạm | PASS | PASS |
| Ghép module PR #3/#4: build | PASS | Cùng bản build |
| Ghép module: sysinfotest | 102/102 PASS | 102/102 PASS |
| Ghép module: forktest | PASS | PASS |
| Ghép module: usertests -q | 60 ca PASS | 60 ca PASS |

Probe chèn lỗi cho collect_file_info trả -1 một lần, sau đó cho
collect_process_info trả -1 một lần. Hai lần syscall đều trả -1, toàn bộ
buffer user vẫn giữ byte 0xA5, rồi 100 lần gọi hợp lệ liên tiếp thành công.
Probe chỉ sửa bản sao build; production không có cờ hoặc hook chèn lỗi.

Bản ghép thử dùng file.c từ PR #3 `45b285aac973e3fd4c8a61914ef7f2c9bb3677ae`,
proc.c/proc.h từ PR #4 `aee410ecf54ce6c8d7ca192929ef64d5a57c2aaf`, và
syscall/sysinfotest/sysfile từ bản sửa PR #2. Đây là kiểm tra tương thích
module, không phải merge các nhánh hoặc thay thế review riêng của PR #3/#4.

sysinfotest bao phủ open, mở trùng path, mode, dup/ref/PID duy nhất,
fork/pipe chia sẻ, process_count, parent/state/zombie/wait, scheduling tăng,
đầy NOFILE/NPROC/NFILE, con trỏ lỗi, mapping chỉ đọc/kernel-only, buffer
nhiều trang/lệch byte, canary, phần dư bằng 0, gọi đồng thời và dup/close
trong lúc chụp 2.000 snapshot. Test kiểm tra ref/PID giữa các phần chỉ khi
writer đứng yên; workload đang biến động chỉ kiểm tra tính hợp lệ từng phần.

ELF của sysinfotest trong môi trường này có LOAD text `R E` và data `RW`.
Nhận định trước đó rằng text luôn RWX không đúng với toolchain đang chạy.
Linker vẫn cảnh báo RWX cho initcode/kernel/forktest, cũng có ở bản gốc.
Các usertrap do ca usertests cố ý truy cập địa chỉ sai kết thúc bằng OK;
chúng không phải kernel panic. Không có kernel panic trong các lượt chạy.

Chạy lại thủ công trong Linux/WSL, với toolchain và QEMU đã cài:

```sh
make clean
make -j4 kernel/kernel fs.img
make qemu CPUS=1
# Trong xv6: sysinfotest; forktest; usertests -q (từng lệnh)
# Thoát QEMU bằng Ctrl-A X, rồi lặp lại với CPUS=3.
```

Log đầy đủ, JSON kết quả, ELF và script điều khiển của phiên được giữ ngoài
nhánh tại `.worktrees/pr2-evidence/`: `baseline-*`, `final-*`, `fault-*`,
`integration-*`, `run_review.py`. Build artifact /tmp không được stage.

## Re-review và giới hạn

Đã rà lại toàn bộ diff PR, không chỉ commit sửa: syscall wiring/ABI,
collector error, bounds, padding, khóa và lifecycle descriptor, fork/exit,
test setup/cleanup và các đường lỗi copyout. Không phát hiện thêm lỗi
nghiêm trọng trong phạm vi tích hợp. git diff --check đạt. Mô phỏng merge
với main chỉ bổ sung docs/review-pr.md, không có xung đột nội dung ở head nền.

Snapshot không nguyên tử toàn hệ thống; bảng file, PID tham chiếu và bảng
process được thu ở những thời điểm khác nhau, slot file có thể được tái sử
dụng. Lỗi copyout ở giữa buffer có thể để lại một phần dữ liệu user; caller
phải bỏ toàn bộ kết quả khi syscall trả -1.

Chưa chạy slowtests, stress dài hạn, hoặc toolchain riscv64-linux-gnu
(không có trong môi trường). Bản ghép module không chứng minh merge Git
sẽ tự giải quyết các phần code trùng nhau. Khi tích hợp chính thức, dùng
collector của đúng thành viên và giữ protocol p->lock thống nhất.

Toàn bộ kiểm thử bắt buộc chạy được đã hoàn tất PASS; đủ điều kiện
commit/push và chuyển reviewer độc lập xem xét Approve trong phạm vi PR #2.
Reviewer độc lập cần kiểm tra commit mới và quyết định Approve; không tự Approve,
merge hay đóng review threads trong phiên này.
