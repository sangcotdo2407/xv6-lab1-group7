# Process Management & CPU Scheduling — Đặng Quang Sang (24120428)

## Phạm vi và đối chiếu yêu cầu

Nhánh: `feature/process-info`, nền `main` tại `1e90d3a`. Phần này đáp ứng mục
4.1/4.2 của `Lab1_File_Process.pdf`: giải thích process/lập lịch và thu thập mọi
tiến trình có `state != UNUSED`, gồm PID, state, PID cha, số lượt chạy và FD.

Không có xung đột chức năng giữa đề và phân công. Hai khác biệt cần ghi nhận:

- DOCX đưa ra kiểu đề xuất `unsigned long scheduling_count`; header đã chốt dùng
  `uint64`. Giữ nguyên header thực tế; trên xv6 RISC-V LP64 hai kiểu có độ rộng
  64 bit. Đề giảng viên không ấn định tên trường hoặc kiểu bộ đếm.
- Đề yêu cầu môi trường Linux; công việc được thực hiện trong repository Windows
  nhưng biên dịch và chạy QEMU bằng Linux qua WSL Ubuntu 24.04. Việc chọn
  xv6-labs-2023, tên nhánh và cách phân công là quy ước nhóm.

## Thiết kế và thay đổi

| File | Thay đổi |
| --- | --- |
| `kernel/proc.h` | Thêm `uint64 scheduling_count` trong nhóm trường bảo vệ bằng `p->lock`; ghi rõ quy tắc khóa `ofile[]`. |
| `kernel/proc.c` | Reset bộ đếm, tăng khi được chọn chạy, cài đặt `collect_process_info()`, tháo descriptor có khóa trước khi đóng trong `exit()`. |
| `kernel/file.c`, `kernel/defs.h` | Dùng giao diện `int file_index_of(struct file *)` có sẵn trong PR #3; định nghĩa một helper kiểm tra miền địa chỉ/căn chỉnh tại nơi sở hữu bảng. |
| `kernel/sysfile.c` | Khóa ngắn quanh các chỗ gán/xóa `ofile[]`: `fdalloc()`, `sys_close()` và hai nhánh rollback của `sys_pipe()`. |
| `tests/process_info_kernel.c` | Kiểm thử kernel trực tiếp, chỉ đưa vào bản sao build tạm. |
| `tests/run_process_info.py` | Tạo bản sao nguồn, build và điều khiển QEMU; có chế độ production không gắn mã kiểm thử. |
| `tests/test_process_info_runner.py` | Kiểm tra vị trí chèn selftest và lỗi khi cấu trúc nguồn không còn phù hợp. |
| `.gitignore` | Bỏ qua log build/test và Python bytecode phát sinh. |
| `docs/process-info-report.md` | Báo cáo kỹ thuật và nội dung vấn đáp. |
| `docs/process-info-tests.md` | Lệnh kiểm thử, bằng chứng và giới hạn thực tế. |
| `docs/process-info-review-response.md` | Đối chiếu từng góp ý của hai reviewer và quy tắc tích hợp. |

`sysfile.c` là phụ thuộc đồng bộ bắt buộc: xv6 gốc cho phép tiến trình tự sửa
`ofile[]` không khóa, vì chưa có tiến trình khác đọc bảng này. Chỉ khóa ở phía
collector vẫn để lại race với writer trên CPU khác. Các thay đổi bổ sung chỉ bảo
vệ việc công bố/tháo con trỏ descriptor; không thay đổi thuật toán quản lý file.
Việc sao chép `np->ofile[]` trong `fork()` vốn đã giữ `np->lock` nên không cần
thêm khóa ở đó. Chủ sở hữu vẫn có thể đọc descriptor của mình không khóa vì
xv6 không có nhiều thread cùng sửa một bảng descriptor của một tiến trình.

Thay đổi ở `file.c/defs.h` chỉ phục vụ helper đã được hai reviewer đề nghị;
chữ ký giống PR #3. Không đổi `kernel/sysinfo.h`, các hàm `collect_*`, syscall
dispatch hoặc user API. `sched()`, `yield()`, `fork()`, `swtch.S` và Makefile
production giữ nguyên mã thực thi.

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
4. Gọi `file_index_of(f)`, chỉ chấp nhận chỉ số trong `[0, NFILE)`. Helper ở
   `file.c` kiểm tra địa chỉ số nguyên, miền và căn chỉnh; không dereference
   con trỏ, không đọc `ref/type`, không lấy `ftable.lock`.
5. Lưu con trỏ cha rồi nhả khóa con. Vẫn giữ `wait_lock` để tránh reparent/reap;
   kiểm tra miền địa chỉ và căn chỉnh của con trỏ cha, lấy riêng khóa cha để đọc
   PID, nhả khóa cha và `wait_lock`. Không có cha thì `parent_pid` giữ giá trị 0.
6. Tăng `process_count`. Nếu có lỗi, nhả hết khóa, xóa lại toàn bộ phần process
   và trả `-1`; caller không nhận một danh sách process dở dang.

Mỗi vòng lặp tạo tối đa một hàng nên không thể vượt `NPROC`; mỗi lần duyệt fd
tạo tối đa một mục nên không thể vượt `NOFILE`; file index đã kiểm tra trong
`[0, NFILE)`. Các phần tử dư và padding trong các hàng process được xóa về 0.

Chỉ `file.c` biết layout của `ftable`; `proc.c` và test không khai báo lại bảng.
Helper ánh xạ địa chỉ vào mảng cố định nên không cần khóa. Địa chỉ ngoài mảng,
NULL hoặc lệch dù chỉ một byte đều trả `-1` trước khi tính chỉ số.

## Khóa, bộ nhớ và giới hạn nhất quán

Thứ tự khóa: `wait_lock → khóa con → nhả khóa con → khóa cha → nhả khóa cha
→ nhả wait_lock`. Đây là thứ tự phù hợp với `wait()`/`exit()`. Collector không
giữ hai khóa process cùng lúc và không lấy `ftable.lock`, khóa inode hoặc pipe.
Mọi đường lỗi đều nhả khóa. Không sleep, cấp phát bộ nhớ, in console hoặc gọi
`copyout()` trong collector.

`fileclose()` có thể gọi `begin_op()/iput()` và sleep. Vì vậy `exit()` và
`sys_close()` tháo descriptor dưới `p->lock`, nhả khóa rồi mới gọi `fileclose()`.
Hai nhánh rollback pipe cũng nhả khóa trước khi đóng file. `exit()` đọc ô của
chính mình để bỏ qua FD trống; chỉ owner sửa bảng nên con trỏ giữ nguyên đến
khi tháo dưới khóa. `fdalloc()` chỉ khóa quanh việc tìm/gán ô.

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

## Tích hợp và vấn đáp

Thứ tự đề xuất: review/merge PR #4 trước; PR #2/#3 sau đó rebase trên main mới.
Giữ đúng một bản process collector, trường counter và điểm tăng/reset của PR #4;
giữ một helper `file_index_of()` đã kiểm tra căn chỉnh; ghép collector file của
Vũ và syscall của Thi. Quy tắc này không tự giải quyết xung đột trên nhánh khác.

Khi vấn đáp cần giải thích được: vì sao writer và reader dùng cùng khóa;
vì sao nhả khóa con trước khi lấy khóa cha; vì sao helper không cần ftable.lock;
vì sao con có counter riêng; và vì sao số lượt dispatch khác CPU time/yield.
Luôn nêu ba giới hạn: snapshot không nguyên tử giữa file/process, index có thể
tái sử dụng, wait_lock chỉ giữ theo từng hàng.

Lệnh chạy, toolchain và kết quả nằm trong [process-info-tests.md](process-info-tests.md).
