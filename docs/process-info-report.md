# Process Management & CPU Scheduling — Đặng Quang Sang (24120428)

## Phạm vi và đối chiếu yêu cầu

Nhánh: `feature/process-info`, nền `origin/main` tại `1e90d3a`.
Đã đọc cả ba trang `Lab1_File_Process.pdf`, toàn bộ nội dung phân công
`Lab1_Nhom7_PhanCong_ThongNhat_Sysinfo.docx`, `sysinfo-conventions.md`,
`review-pr.md`, `kernel/sysinfo.h`, `kernel/defs.h` và mã nguồn liên quan.

Đề giảng viên mục 4.1 yêu cầu giải thích `pid/state/context`, luồng `fork()` và
`scheduler()/sched()/yield()`. Mục 4.2 yêu cầu thống kê mọi tiến trình có
`state != UNUSED`, gồm PID, trạng thái, PID cha, số sự kiện lập lịch và file đang
mở. Phần này thực hiện đúng các yêu cầu đó trong phạm vi của Sang.

Không có xung đột chức năng giữa đề và phân công. Hai khác biệt cần ghi nhận:

- DOCX đưa ra kiểu đề xuất `unsigned long scheduling_count`; header đã chốt dùng
  `uint64`. Giữ nguyên header thực tế; trên xv6 RISC-V LP64 hai kiểu có độ rộng
  64 bit. Đề giảng viên không ấn định tên trường hoặc kiểu bộ đếm.
- Đề yêu cầu môi trường Linux; công việc được thực hiện trong repository Windows
  nhưng biên dịch và chạy QEMU bằng Linux qua WSL Ubuntu 24.04. Việc chọn
  xv6-labs-2023, tên nhánh và cách phân công là quy ước nhóm.

Phần bàn giao này chưa thực hiện giai đoạn tích hợp toàn nhóm hoặc đóng gói
bài nộp. Commit và PR riêng cho `feature/process-info` phục vụ review trước
khi tích hợp; không tự merge vào `main`. Những hướng dẫn nộp bài trong tài liệu
là bối cảnh, không phải yêu cầu thực hiện trong phần công việc này.

## Thiết kế và thay đổi

| File | Thay đổi |
| --- | --- |
| `kernel/proc.h` | Thêm `uint64 scheduling_count` trong nhóm trường bảo vệ bằng `p->lock`; ghi rõ quy tắc khóa `ofile[]`. |
| `kernel/proc.c` | Reset bộ đếm, tăng khi được chọn chạy, cài đặt `collect_process_info()`, tháo descriptor có khóa trước khi đóng trong `exit()`. |
| `kernel/sysfile.c` | Khóa ngắn quanh các chỗ gán/xóa `ofile[]`: `fdalloc()`, `sys_close()` và hai nhánh rollback của `sys_pipe()`. |
| `tests/process_info_kernel.c` | Kiểm thử kernel trực tiếp, chỉ đưa vào bản sao build tạm. |
| `tests/run_process_info.py` | Tạo bản sao nguồn, build và điều khiển QEMU; có chế độ production không gắn mã kiểm thử. |
| `.gitignore` | Bỏ qua log build/test và Python bytecode phát sinh. |
| `docs/process-info-report.md` | Báo cáo kỹ thuật và nội dung vấn đáp. |
| `docs/process-info-tests.md` | Lệnh kiểm thử, bằng chứng và giới hạn thực tế. |

`sysfile.c` là phụ thuộc đồng bộ bắt buộc: xv6 gốc cho phép tiến trình tự sửa
`ofile[]` không khóa, vì chưa có tiến trình khác đọc bảng này. Chỉ khóa ở phía
collector vẫn để lại race với writer trên CPU khác. Các thay đổi bổ sung chỉ bảo
vệ việc công bố/tháo con trỏ descriptor; không thay đổi thuật toán quản lý file.
Việc sao chép `np->ofile[]` trong `fork()` vốn đã giữ `np->lock` nên không cần
thêm khóa ở đó. Chủ sở hữu vẫn có thể đọc descriptor của mình không khóa vì
xv6 không có nhiều thread cùng sửa một bảng descriptor của một tiến trình.

Không sửa `kernel/file.c`, `kernel/sysinfo.h`, `kernel/defs.h`, syscall dispatch,
user API hoặc `user/sysinfotest.c`. `sched()`, `yield()`, `fork()` và `swtch.S`
giữ nguyên mã thực thi. Makefile production không nạp mã kiểm thử kernel.

## Cách collect_process_info() hoạt động

Hàm nhận vùng nhớ kernel riêng do caller chuẩn bị. Trả `0` khi thành công;
trả `-1` khi con trỏ buffer bằng `0` hoặc gặp con trỏ file/cha ngoài bảng hay
sai căn chỉnh. Nó không xác thực được một con trỏ kernel tùy ý; địa chỉ user
phải được xử lý bởi syscall và `copyout()`, không truyền trực tiếp vào hàm này.

1. Đặt `process_count = 0` và xóa toàn bộ `processes[]`. Không ghi vào phần
   thống kê file do Vũ phụ trách.
2. Duyệt đúng `NPROC` ô. Lấy `wait_lock` rồi `p->lock`; bỏ qua `UNUSED`, giữ lại
   cả `USED`, `SLEEPING`, `RUNNABLE`, `RUNNING`, `ZOMBIE`.
3. Chép PID, trạng thái, bộ đếm và các descriptor dưới `p->lock`. Duyệt đúng
   `NOFILE` ô; mỗi ô khác `0` tạo một phần tử `{fd, file_index}`. Các phần tử
   được xếp liên tiếp theo thứ tự fd tăng dần; `open_file_count` là số descriptor.
4. Với con trỏ file, chuyển địa chỉ thành `uint64`, kiểm tra miền địa chỉ của
   `ftable.file[]` và chia hết cho `sizeof(struct file)`, sau đó tính chỉ số.
   Không trừ hai con trỏ C không cùng mảng, không dereference con trỏ file,
   không đọc `ref/type` hoặc giữ `ftable.lock`.
5. Lưu con trỏ cha rồi nhả khóa con. Vẫn giữ `wait_lock` để tránh reparent/reap;
   kiểm tra miền địa chỉ và căn chỉnh của con trỏ cha, lấy riêng khóa cha để đọc
   PID, nhả khóa cha và `wait_lock`. Không có cha thì `parent_pid` giữ giá trị 0.
6. Tăng `process_count`. Nếu có lỗi, nhả hết khóa, xóa lại toàn bộ phần process
   và trả `-1`; caller không nhận một danh sách process dở dang.

Mỗi vòng lặp tạo tối đa một hàng nên không thể vượt `NPROC`; mỗi lần duyệt fd
tạo tối đa một mục nên không thể vượt `NOFILE`; file index đã kiểm tra trong
`[0, NFILE)`. Các phần tử dư và padding trong các hàng process được xóa về 0.

`ftable` hiện là đối tượng global có external linkage trong `file.c`. Collector
khai báo `extern` tương thích với đúng layout hiện tại, chỉ sử dụng địa chỉ mảng
cố định. Vì vậy không cần thay đổi API chung hoặc đợi helper của thành viên
khác. Khi tích hợp, nếu Vũ đổi layout hoặc chuyển `ftable` thành `static`, cả
nhóm phải thống nhất helper ánh xạ trong `file.c`; không tự sửa giao diện ở đây.

## Khóa, bộ nhớ và giới hạn nhất quán

Thứ tự khóa: `wait_lock → khóa con → nhả khóa con → khóa cha → nhả khóa cha
→ nhả wait_lock`. Đây là thứ tự phù hợp với `wait()`/`exit()`. Collector không
giữ hai khóa process cùng lúc và không lấy `ftable.lock`, khóa inode hoặc pipe.
Mọi đường lỗi đều nhả khóa. Không sleep, cấp phát bộ nhớ, in console hoặc gọi
`copyout()` trong collector.

`fileclose()` có thể gọi `begin_op()/iput()` và sleep. Vì vậy `exit()` và
`sys_close()` tháo descriptor dưới `p->lock`, nhả khóa rồi mới gọi `fileclose()`.
Hai nhánh rollback pipe cũng nhả khóa trước khi đóng file. Khóa mới trong
`fdalloc()` chỉ bao quanh việc tìm/gán ô; không bao quanh thao tác filesystem.

Caller phải cung cấp buffer đủ lớn và không cho nhiều caller dùng chung cùng
lúc nếu chưa có cơ chế tuần tự hóa. Collector không tạo `struct sysinfo` cục bộ
hay giữ buffer dùng chung. Với header hiện tại, kết quả đầy đủ là 38.248 byte,
lớn hơn kernel stack một trang 4.096 byte.

Snapshot nhất quán theo từng hàng về metadata và danh sách descriptor, không
phải ảnh chụp nguyên tử của toàn hệ thống. Trạng thái có thể đã đổi khi syscall
trả về; process mới xuất hiện trong ô đã quét có thể chưa được thấy. Hai lần
thu thập file và process có thể khác thời điểm; một file index có thể được tái
sử dụng giữa hai lần thu thập. Không suy ra rằng `ref` luôn bằng số descriptor
trong snapshot. `wait_lock` chỉ giữ suốt một hàng để giảm thời gian chặn exit/wait.

## struct proc và vòng đời tiến trình

`struct proc` là bản ghi kernel của một tiến trình trong bảng cố định `proc[NPROC]`.
`pid` định danh một lần tồn tại của tiến trình, khác chỉ số ô trong bảng.
`state`, `pid`, `scheduling_count` và các trường điều khiển được bảo vệ bằng
`p->lock`; quan hệ `parent` được bảo vệ bằng `wait_lock`.

| Trạng thái | Ý nghĩa |
| --- | --- |
| `UNUSED` | Ô trống, không đưa vào snapshot. |
| `USED` | Đã được cấp phát nhưng chưa sẵn sàng chạy. |
| `SLEEPING` | Đợi một sự kiện trên `chan`. |
| `RUNNABLE` | Đủ điều kiện để scheduler chọn. |
| `RUNNING` | Đang được một CPU thực thi. |
| `ZOMBIE` | Đã exit, còn thông tin để cha gọi wait. |

Các trường còn lại chứa kernel stack, kích thước/vùng nhớ user, pagetable,
trapframe, context kernel, bảng descriptor, thư mục hiện tại và tên tiến trình.
`exit()` đóng file, chuyển con sang init, báo cha rồi chuyển thành `ZOMBIE`;
`wait()` mới gọi `freeproc()` để giải phóng tài nguyên và đưa ô về `UNUSED`.

## Luồng fork()

`ecall → usertrap() → syscall() → sys_fork() → fork()`.

`allocproc()` tìm một ô `UNUSED` có khóa, cấp PID mới, đặt `USED`, reset
`scheduling_count = 0`, cấp trapframe/pagetable và chuẩn bị context với
`ra = forkret`, `sp = đỉnh kernel stack`. Nếu cấp phát thất bại, `freeproc()`
dọn phần đã cấp và trả lỗi. `freeproc()` cũng reset bộ đếm về 0.

`uvmcopy()` sao chép bộ nhớ user; trapframe được sao chép và đặt `a0 = 0` cho
con. Các descriptor con tham chiếu cùng `struct file` với cha thông qua
`filedup()`, nên dùng chung file offset và tăng ref. Thư mục hiện tại dùng
`idup()`. Kernel stack và context kernel của cha không được sao chép sang con.

Sau khi nhả khóa con, `fork()` gán cha dưới `wait_lock`, rồi lấy khóa con để
đặt `RUNNABLE`. Cha nhận PID con từ giá trị trả về của `fork()`; con bắt đầu
tại `forkret()`, rồi trở về user bằng trapframe nên nhận 0. Bộ đếm của con bắt
đầu độc lập, không kế thừa số lần lập lịch của cha.

## scheduler(), sched(), yield() và context switching

Mỗi CPU chạy `scheduler()` riêng. Nó bật interrupt, duyệt bảng process theo
thuật toán gốc, lấy `p->lock`; nếu thấy `RUNNABLE`, nó đặt `RUNNING`, tăng
`scheduling_count` đúng một lần, gán `c->proc` rồi gọi
`swtch(&c->context, &p->context)`. Khi process trả CPU, scheduler xóa `c->proc`
và nhả khóa. Không thay đổi thứ tự duyệt, điều kiện chọn hoặc ưu tiên.

`yield()` lấy khóa của chính process, đặt `RUNNABLE`, gọi `sched()`, rồi nhả
khóa khi được chạy lại. `yield()` không tăng bộ đếm. Việc chỉ chuyển trạng thái
hoặc được `wakeup()` đưa về `RUNNABLE` chưa phải một lần được lập lịch.

`sched()` kiểm tra: đang giữ `p->lock`, độ sâu tắt interrupt là 1 (không giữ
thêm spinlock), trạng thái không còn `RUNNING` và interrupt đang tắt. Nó lưu
`intena`, gọi `swtch(&p->context, &mycpu()->context)` để trở lại scheduler và
khôi phục `intena` khi được tiếp tục. `sleep()` và `exit()` cũng đi qua `sched()`.

`swtch.S` lưu/khôi phục `ra`, `sp`, `s0..s11` theo ABI RISC-V. Nó nhận địa chỉ
hai context qua `a0/a1`; `ret` đi đến địa chỉ trong `ra` vừa khôi phục, còn `sp`
chuyển sang kernel stack tương ứng. Nó không đổi trực tiếp giữa hai process:
luồng thông thường là process A → scheduler của CPU → process B. Các thanh ghi
caller-saved được xử lý theo quy tắc gọi hàm; context switch không cần lưu lại
toàn bộ thanh ghi user.

`context` khác `trapframe`: context để tiếp tục luồng kernel qua `swtch()`,
trapframe giữ thanh ghi user và PC user cho trap/syscall và đường trở lại user
qua trampoline. `swtch()` không tự chuyển pagetable hoặc thực hiện `sret`.
Khóa process được chuyển qua ranh giới context switch: scheduler giữ khóa khi
vào process; `forkret()` hoặc đường quay lại `sched()` sẽ nhả nó đúng nơi.

Bộ đếm đo số lượt được scheduler chọn, không đo CPU time, số timer tick,
số syscall, số lần sleep hay tổng số lời gọi `swtch()`. Một dispatch có thể do
preemption, tự yield, hoặc tỉnh dậy từ sleep. Kiểu `uint64` sẽ wrap theo phép
toán unsigned sau 2^64 lượt; không đổi nghĩa thành bộ đếm bão hòa.

## Ghi nhớ khi vấn đáp

- Vì sao khóa collector thôi chưa đủ? Vì writer cũng phải dùng cùng khóa;
  `ofile[]` của xv6 gốc chỉ có chủ sở hữu đọc/ghi nên trước đây không cần khóa.
- Vì sao không lấy khóa cha khi đang giữ khóa con? Để tránh chu trình giữa
  nhiều khóa process; `wait_lock` giữ ổn định quan hệ trong lúc đổi khóa.
- Vì sao không cần `ftable.lock` để tính file index? Chỉ tính vị trí trong mảng
  tĩnh, không đọc nội dung đối tượng có thể thay đổi.
- Vì sao con không sao chép bộ đếm cha? `allocproc()` tạo một lần sống mới với
  context riêng; `fork()` chỉ sao chép trapframe, bộ nhớ user và các tham chiếu.
- Tại sao không tăng ở `yield()`? Một lượt chạy còn có thể bắt đầu sau sleep,
  lần chạy đầu tiên hoặc sau wakeup; chỉ scheduler quyết định dispatch thực tế.
- Có phải snapshot cho biết toàn hệ thống tại một thời điểm duy nhất? Không;
  phải trình bày rõ tính nhất quán theo hàng và khả năng file index tái sử dụng.
- Vì sao không khai báo toàn bộ sysinfo trên stack? Kích thước vượt nhiều lần
  stack kernel một trang; syscall phải có vùng đệm đủ lớn và đồng bộ caller.

Kết quả chạy và phần chưa kiểm chứng được ghi riêng trong
[process-info-tests.md](process-info-tests.md).
