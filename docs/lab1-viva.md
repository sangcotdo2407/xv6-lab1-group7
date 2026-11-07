# Lab 1 — Đọc code và chuẩn bị vấn đáp

Tài liệu dựa trên cây `integration/lab1` sau tích hợp PR #2/#3/#4 và sửa
đồng bộ metadata. [Kết quả kiểm thử](lab1-integration-review.md).

## Luồng sysinfo từ user đến kernel và trở lại

1. `user/sysinfotest.c` dùng `static struct sysinfo si`, gọi `sysinfo(&si)`.
   Buffer khoảng 38 KB nên không đặt trên stack một trang. Prototype trong
   user.h có forward declaration `struct sysinfo`; cùng header định nghĩa ABI.
2. Makefile chạy usys.pl để sinh stub trong usys.S. Theo ABI RISC-V, địa chỉ
   `&si` ở a0; stub đặt `SYS_sysinfo = 22` vào a7 rồi thực hiện `ecall`.
3. Phần cứng chuyển sang supervisor mode. `trampoline.S:uservec` lưu thanh ghi
   user vào trapframe, dùng kernel pagetable/stack và chuyển tới `usertrap()`.
4. usertrap nhận scause=8, kiểm tra killed, tăng epc 4 byte để bỏ qua ecall,
   bật interrupt rồi gọi `syscall()`. Dispatcher đọc a7, chọn sys_sysinfo từ bảng.
5. `sys_sysinfo()` gọi `argaddr(0, &uaddr)` để lấy a0. argaddr trả void và chưa
   chứng minh địa chỉ hợp lệ; kernel không dereference uaddr trực tiếp.
6. Lấy sleeplock kinfo_lock, xóa buffer tĩnh kinfo. Mỗi syscall dùng buffer
   riêng ở user nhưng buffer kernel được chia sẻ, nên phải tuần tự hóa.
7. `collect_file_info(&kinfo)` chụp các slot ref>0 dưới ftable.lock, nhả khóa,
   rồi duyệt ofile của từng process dưới p->lock để ghi PID không trùng.
8. `collect_process_info(&kinfo)` lấy wait_lock rồi p->lock theo từng hàng,
   bỏ UNUSED; chép PID/state/counter/FD. Nhả khóa con trước khi lấy khóa cha
   để đọc parent PID, vẫn giữ wait_lock cho parentage/lifetime ổn định.
9. Nếu một collector trả -1, không gọi copyout. Nếu cả hai thành công,
   `copyout(pagetable, uaddr, &kinfo, sizeof(kinfo))` dịch địa chỉ từng trang,
   kiểm tra MAXVA/PTE_V/PTE_U/PTE_W rồi chép dữ liệu. Không còn giữ khóa bảng.
10. Mọi đường đi đều nhả sleeplock. Hàm trả 0 hoặc -1; dispatcher ghi vào
    trapframe->a0. usertrapret chuẩn bị trở về, trampoline:userret đổi sang
    user pagetable, khôi phục thanh ghi rồi `sret`. Stub tiếp tục tại `ret`,
    chương trình user nhận giá trị trả và đọc si khi thành công.

```mermaid
sequenceDiagram
    participant U as sysinfotest
    participant S as stub / ecall
    participant K as usertrap / syscall
    participant I as sys_sysinfo
    participant C as collectors
    U->>S: sysinfo(&si), a0=&si
    S->>K: a7=22, ecall
    K->>I: syscalls[22]()
    I->>I: argaddr, sleeplock, zero kinfo
    I->>C: collect_file_info rồi collect_process_info
    C-->>I: 0 hoặc -1, đã nhả spinlock
    I->>I: copyout nếu thành công; nhả sleeplock
    I-->>K: 0 hoặc -1
    K-->>S: a0, usertrapret / sret
    S-->>U: ret, dữ liệu hợp lệ khi trả 0
```

## Cấu trúc và hàm cần nắm

| Thành phần | Cần giải thích được |
| --- | --- |
| proc[NPROC], struct proc | PID khác vị trí slot; state, parent, context, trapframe, ofile, scheduling_count. |
| ftable.file[NFILE], struct file | Đối tượng mở toàn hệ thống; type/ref/mode, inode hoặc pipe và offset. |
| struct sysinfo_file | Một hàng file_index/type/mode/ref và PID không trùng. |
| struct sysinfo_fd | Cặp fd cục bộ → file_index toàn hệ thống. |
| struct sysinfo_process | PID/state/parent/counter và danh sách FD của một process. |
| struct sysinfo | Hai count và hai mảng cố định; cùng layout ở user/kernel, xóa cả padding. |
| sys_open, filealloc, fdalloc | Inode/path/transaction → object → descriptor → cấu hình type/mode → trả fd. |
| filesetinfo | Công bố type/mode dưới cùng khóa với collector; không đọc inode/pipe hoặc thay ref. |
| filedup, fileclose | Tăng/giảm một ref; fileclose cuối giải phóng tài nguyên sau khi nhả ftable.lock. |
| file_index_of | Kiểm miền/căn chỉnh bằng địa chỉ số nguyên, không dereference, không lấy ftable.lock. |
| collect_file_info | slot_of ánh xạ vị trí bảng sang hàng nén; hai pha không giữ đồng thời khóa file/process. |
| collect_process_info | Lấy wait_lock trước p->lock, lấy riêng khóa cha; chỉ ghi phần process. |
| allocproc/freeproc/fork/exit/wait | Vòng đời slot, reset counter, chia sẻ file, zombie rồi reap. |
| scheduler/sched/yield/swtch | Chọn process, trả CPU và chuyển context là các việc khác nhau. |
| sys_sysinfo/argaddr/copyout | Điều phối, lấy địa chỉ, tuần tự hóa, xử lý lỗi và chép theo pagetable. |
| syscalls[]/SYS_sysinfo/usys.pl | Đồng nhất số syscall 22 từ user stub đến handler. |

## Những quyết định thiết kế cần giải thích

- `open_file_count` toàn hệ thống đếm object có ref>0; count trong process
  đếm FD. Một PID có nhiều FD tới cùng file chỉ ghi một lần trong pids.
- Giữ header chung và API; helper filesetinfo thêm vào phạm vi kernel để mọi
  writer metadata dùng cùng khóa. Không sao chép layout ftable sang proc.c.
- Filealloc xóa mode trước ref=1. Trước filesetinfo, object đang cấp phát có
  FD_NONE/mode0; đây là trạng thái trung gian hợp lệ trong snapshot.
- Chỉ khóa reader không đủ: writer descriptor cũng phải dùng p->lock. Fork
  đã giữ np->lock khi điền bảng con; owner đọc bảng riêng không cần khóa vì
  xv6 không có nhiều thread cùng sửa bảng của một process.
- Không giữ p->lock khi fileclose: pipeclose có thể wakeup, iput/begin_op có
  thể sleep. Ngược lại, helper ánh xạ chỉ số chỉ dùng địa chỉ nên không cần
  ftable.lock khi đang giữ khóa process.
- Buffer tĩnh+sleeplock là cách ngắn, dễ giải thích, không cần cấp nhiều
  trang kernel hoặc giữ spinlock trong toàn bộ syscall.
- Dữ liệu dư bằng 0 để không lộ dữ liệu cũ, giữ giao diện ổn định và dễ kiểm.
- Counter uint64 tăng một lần tại RUNNABLE→RUNNING, dưới p->lock; reset ở
  allocproc/freeproc, không kế thừa counter của cha. Không thêm lần tăng ở yield.
- Các pha có thời điểm khác nhau. Test đang churn kiểm bounds/PID duy nhất;
  chỉ test đã đồng bộ cho writer đứng yên mới đòi ref bằng tổng FD và liên kết
  file/process trùng khớp. Không khẳng định snapshot nguyên tử toàn hệ thống.
- -1 có nghĩa phải bỏ kết quả. Copyout lỗi ở trang sau có thể đã ghi một phần
  user buffer; lỗi collector thì hoàn toàn không copyout.

## Hai luồng và một bảng cần nhớ

`open → sys_open → begin_op → create/namei + ilock → filealloc → fdalloc →`
`chuẩn bị inode/offset/major → filesetinfo → itrunc nếu cần → iunlock/end_op → fd`.
Nếu filealloc/fdalloc thất bại, đóng tham chiếu đã cấp và dọn inode/transaction.

`fork → allocproc (USED, counter=0, trapframe/context) → uvmcopy → sao chép`
`trapframe và a0=0 → filedup từng FD + idup(cwd) → parent dưới wait_lock →`
`RUNNABLE → scheduler chọn → forkret → user`. Cha nhận PID con, con nhận 0;
context kernel và kernel stack không sao chép nguyên từ cha.

| Lệnh | FD | struct file / offset | Ref / PID |
| --- | --- | --- | --- |
| open cùng path hai lần | Hai FD | Hai object, offset độc lập | Mỗi object ref=1 |
| dup(fd) | FD mới trong cùng process | Cùng object/offset | Ref+1, số PID không tăng |
| fork() | Bảng FD riêng cho con | Cùng object/offset với cha | Ref+1 cho mỗi FD kế thừa; có PID con |
| close(fd) | Xóa một FD | Object tồn tại nếu còn ref | Ref-1, giải phóng khi về 0 |

`scheduler()` chọn RUNNABLE, đặt RUNNING và tăng counter trước swtch.
`yield()` đặt RUNNABLE rồi gọi sched. `sched()` yêu cầu p->lock, interrupt tắt,
độ sâu khóa 1 và state khác RUNNING, rồi swtch về scheduler. swtch lưu ra/sp và
s0..s11 trong context; trapframe mới chứa trạng thái user cho trap/sret.

## 10 câu hỏi vấn đáp

1. **Một lời gọi sysinfo đi qua đâu?**
   User stub đặt a7=22 → ecall → usertrap → syscall → sys_sysinfo → hai collector
   → copyout → trapframe a0 → usertrapret/userret/sret → stub ret về caller.

2. **ofile[], ftable.file[] và file_index khác nhau thế nào?**
   ofile[fd] là ánh xạ trong một process; ftable là bảng object dùng chung.
   file_index là chỉ số object trong ftable, khác fd và vị trí hàng kết quả.

3. **Vì sao ref có thể là 3 nhưng process_count chỉ là 1?**
   Một process giữ ba FD cùng object qua dup. Ref đếm ba tham chiếu; PID chỉ
   được liệt kê một lần. Fork tăng ref theo từng FD kế thừa và thêm PID con.

4. **Vì sao cần khóa writer và thêm filesetinfo?**
   Khóa collector không ngăn writer ngoài khóa. p->lock bảo vệ gán/xóa ofile;
   ftable.lock bảo vệ ref và công bố type/mode. filesetinfo làm ba trường
   metadata được quan sát cùng một lần cập nhật, không thêm kiến trúc khóa mới.

5. **Vì sao nhả p->lock trước fileclose và khóa con trước khóa cha?**
   Fileclose có thể sleep/wakeup nên không gọi dưới p->lock. Hai khóa proc
   đồng thời có thể đảo thứ tự; wait_lock ổn định quan hệ cha khi đổi khóa.

6. **Bộ đếm tăng ở đâu, có giống số lần yield hay CPU time không?**
   Tăng dưới p->lock đúng lúc scheduler chọn RUNNABLE→RUNNING. Nó đếm dispatch,
   gồm cả tỉnh từ sleep và preemption; không đo thời gian CPU hay số yield.

7. **Vì sao dùng buffer tĩnh và sleeplock?**
   Struct 38.248 byte vượt stack 4.096 byte. Buffer dùng chung cần tuần tự hóa;
   sleeplock cho caller đang chờ được ngủ, không giữ spinlock suốt syscall.

8. **argaddr có bảo đảm con trỏ hợp lệ không? Lỗi xử lý thế nào?**
   Không; nó chỉ lấy a0. Copyout kiểm mapping/quyền từng trang. Lỗi trả -1 và
   luôn nhả sleeplock; lỗi collector bỏ copyout, lỗi copyout có thể ghi một phần.

9. **Snapshot có bắt buộc ref bằng tổng FD không?**
   Chỉ khi writer đứng yên. Các pha lấy mẫu khác thời điểm và slot có thể tái
   sử dụng; khi có churn chỉ bảo đảm protocol khóa, giới hạn và dữ liệu từng phần.

10. **Chứng minh code đúng bằng những test nào?**
    Sysinfotest 102/102, filetest 49 check, forktest, usertests 60 ca trên 1/3 CPU;
    10.000 snapshot file churn, 32 dispatch chính xác, full tables, con trỏ lỗi,
    sanitizer, fault collector giữ buffer 0xA5 và 100 recovery calls, stressfs
    kiểm nội dung. Chưa khẳng định kết quả slowtests hoặc stress dài hạn.
