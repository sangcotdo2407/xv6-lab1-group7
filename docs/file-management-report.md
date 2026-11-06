# File Management — Mai Khắc Hoàng Vũ, 24120183

## Phạm vi và căn cứ

Ưu tiên đề `Lab1_File_Process.pdf`, sau đó `docs/sysinfo-conventions.md`
và `docs/review-pr.md`, cuối cùng tài liệu phân công. Tài liệu phân công được
người dùng xác nhận là `Lab1_Nhom7_PhanCong_ThongNhat_Sysinfo.docx`, mục 4.1.

Vũ phụ trách khảo sát file, collector file và test open/dup/close/fork.
Syscall/stub/copyout và bộ test chung thuộc Thi (PR #2). Collector tiến trình
và scheduling thuộc Sang (PR #4). Thay đổi trong proc.c/proc.h chỉ tháo khóa
ofile_lock do PR #3 thêm, thống nhất bảo vệ ofile[] với PR #4 và phục hồi kiểm
tra lỗi/xóa phần dư cho giao diện descriptor đã có trong PR #2; không thay
thuật toán lập lịch hay giao diện sysinfo.h/defs.h.

## ofile[], ftable.file[] và struct file

`p->ofile[fd]` ánh xạ descriptor cục bộ sang đối tượng `struct file` trong
`ftable.file[]`. Giá trị fd chỉ có nghĩa trong một process. `file_index`
là vị trí object trong bảng file toàn hệ thống, không phải fd hoặc vị trí
hàng trong kết quả đã nén. `slot_of[]` ánh xạ chỉ số bảng sang hàng kết quả.

`struct file` chứa type, ref, readable/writable, pipe hoặc inode, offset và
major cho device. `filealloc()` chọn slot ref == 0 và đặt ref = 1;
`filedup()` tăng ref; `fileclose()` giảm ref và giải phóng tài nguyên khi về
0. Các thao tác ref được bảo vệ bằng ftable.lock.

| Thao tác | Descriptor/object/ref |
| --- | --- |
| open() | Một descriptor mới, một struct file mới, ref = 1; hai lần mở cùng đường dẫn vẫn là hai object |
| dup(fd) | Descriptor mới cùng object, ref tăng 1, dùng chung offset; không tăng số object hoặc số PID |
| fork() | Bảng descriptor riêng cho con, nhưng các con trỏ dùng chung object; mỗi fd kế thừa tăng ref 1 |
| close(fd) | Xóa descriptor và giảm ref; object còn tồn tại nếu vẫn có tham chiếu |

Một process có nhiều fd cùng object chỉ xuất hiện một lần trong pids[].
Các fd của một process được duyệt liền nhau, nên so PID cuối đã ghi là đủ
để loại trùng. Giới hạn process_count là NPROC; các vòng duyệt file/fd bị
giới hạn bởi NFILE/NOFILE.

## Luồng open()

User stub chuyển SYS_open đến sys_open(). Kernel lấy path và mode, bắt đầu
filesystem transaction, tra hoặc tạo inode, kiểm tra directory/device,
cấp struct file rồi descriptor. Kernel đặt type/mode/inode/offset,
mở khóa inode, kết thúc transaction và trả fd. Nếu cấp descriptor thất
bại, tham chiếu file được đóng và transaction kết thúc.

`fdalloc()` nhận quyền sở hữu tham chiếu của caller khi thành công;
`sys_dup()` gọi filedup cho descriptor mới. Trong fork(), np->lock đã giữ
từ allocproc bảo vệ bảng của con. Xv6 không có nhiều thread cùng ghi bảng
của một process, nên process sở hữu có thể đọc descriptor của chính mình
mà không lấy thêm khóa parent trong lúc đang giữ khóa child.

## Thu thập, khóa và giới hạn snapshot

Pha 1 giữ ftable.lock để chụp các slot ref > 0. Pha 2 đã nhả ftable.lock,
giữ từng p->lock để đọc PID và bảng descriptor. Mọi writer của ofile[] dùng
p->lock: fdalloc, close, exit và rollback pipe; fork ghi bảng con dưới
np->lock. Collector process cũng dùng p->lock. Đây là protocol của PR #4.

Collector không giữ ftable.lock cùng p->lock. fileclose có thể sleep hoặc
wakeup, nên sys_close/exit/rollback luôn xóa descriptor dưới p->lock rồi
nhả khóa trước khi fileclose. Không thêm khóa parent vào fork khi đã giữ
np->lock. Khóa collector process vẫn theo thứ tự wait_lock rồi p->lock.

Snapshot không nguyên tử toàn hệ thống. Các hàng và hai pha có thể phản
ánh các thời điểm khác nhau: ref, PID và danh sách FD có thể không khớp
khi có open/dup/close/fork/exit đồng thời; slot có thể được tái sử dụng.
Object đang được khởi tạo sau filealloc cũng có thể tạm có type FD_NONE.
Không suy diễn ref bằng tổng FD từ các pha khác nhau khi hệ thống biến động.

Trong trạng thái ổn định, open cùng đường dẫn cho index khác nhau,
dup/fork cho cùng index và ref tăng đúng. Collector không thay đổi ref
hoặc giữ thêm tham chiếu để chụp dữ liệu, nên không làm sai ref quan sát.

file_index_of chỉ dùng địa chỉ số nguyên, kiểm tra miền và phần dư theo
sizeof(struct file); không dereference đối số. Collector kiểm tra index
trước slot_of[]. NULL descriptor được bỏ qua; con trỏ khác NULL nhưng
không hợp lệ làm collector trả -1 và xóa phần kết quả file. Phần process
được giữ nguyên. Collector process có quy tắc tương ứng cho phần của nó.
Caller dùng buffer đủ lớn ngoài kernel stack; syscall hiện tại xử lý mã
lỗi, tuần tự hóa buffer và không copyout kết quả thiếu.

## Kiểm thử và chạy lại

Yêu cầu Linux/WSL, gcc native có ASan/UBSan, toolchain
riscv64-unknown-elf và qemu-system-riscv64:

```sh
python3 tests/run_file_info.py --cpus 1 3 --quick-suite
# Kiểm thử thêm với checkout PR #4, chỉ lấy proc.c/proc.h/sysfile.c:
python3 tests/run_file_info.py --cpus 1 3 --quick-suite --process-source /path/to/pr4
# Probe kernel chỉ chèn vào bản sao build:
python3 tests/run_file_info.py --kernel-probe-only --cpus 1 3
# Tích hợp thêm nguyên syscall/test của Thi:
python3 tests/run_file_info.py --integration-only --cpus 1 3 \
  --process-source /path/to/pr4 --syscall-source /path/to/pr2
```

Runner build trên bản sao tạm; không chèn probe vào kernel production.
Log ở test-results/file-info. filetest chạy trực tiếp từ shell xv6;
sysinfotest tiếp tục là chương trình test syscall của Thi.

- Native: helper và collector trên nguyên file.c; NULL, con trỏ ngoài bảng,
  mọi offset lệch trong một struct file, slot 0/NFILE-1, bảng rỗng/đầy,
  NFILE/NPROC/NOFILE, PID không trùng, ref khác số PID, guard, xóa dư,
  nhả khóa và bảo toàn phần process khi thành công/thất bại.
- Native formatter: mã putu thực tế với 0, 2^31, 2^32, UINT64_MAX.
- Kernel probe: object thật từ filealloc, collector file/process trên
  buffer điền dữ liệu cũ, mọi offset lệch trong struct file, lỗi trả -1,
  phần dư bằng 0, nhả khóa và tiếp tục boot sau cleanup.
- filetest: open/dup/fork/close/pipe, bảng descriptor đầy, rollback khi
  thiếu 0/1 slot, lỗi copyout ở fd đầu/thứ hai, descriptor lỗi và 10.000
  snapshot với 4 worker dup/close, open/close, pipe và pipe rollback.
- sysinfotest: các ca syscall, mode, PID/ref, giới hạn NFILE/NPROC/NOFILE,
  con trỏ user lỗi, buffer vượt trang, phần dư và caller đồng thời.
- forktest và usertests -q kiểm tra hồi quy xv6.

Không đòi hỏi ref bằng tổng FD trong test churn. Fault injection con trỏ
kernel chỉ có trong harness native, không là bằng chứng user tạo được
con trỏ lệch qua syscall hợp lệ.

## Tích hợp và artifact

Giữ nguyên kernel/sysinfo.h và chữ ký hàm. file_index_of đồng nhất với
PR #4; các writer dùng cùng p->lock. Khi giải quyết conflict proc.c, dùng
collector process chính thức của Sang. Hàm in uint64 trong sysinfotest
giữ cách làm đã có trong PR #2, không sửa thư viện printf chung.

Ba snapshot patch cũ (`kernel/24120183_file_part_v2.patch`,
`user/24120183_file_on_sysinfo_v2.patch`, `user/test_only_sysinfo_wiring.patch`)
đã bỏ khỏi cây nguồn vì khác code/test đang chạy; wiring test-only còn
bỏ qua process collector và khóa buffer. Chúng không phải patch phát hành.
Sang tạo patch nộp cuối từ cây tích hợp đã kiểm thử, bao gồm file mới;
không áp dụng lại các snapshot cũ lên code hiện tại.

Chưa chạy slowtests hoặc stress dài hạn. Cảnh báo linker RWX của
initcode/kernel/forktest thuộc cơ chế build nền, không sửa linker trong
PR File Management. Kết quả thực tế và trạng thái review lại được ghi
trong docs/file-management-fix-results.md sau khi các lượt test kết thúc.
