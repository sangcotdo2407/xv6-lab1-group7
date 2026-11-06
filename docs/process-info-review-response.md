# Xử lý review PR #4 — Process Management & CPU Scheduling

Đối chiếu hai phản hồi tại head `5e60f38`:

- [Hồ Sỹ Thi / thihovn](https://github.com/sangcotdo2407/xv6-lab1-group7/pull/4#issuecomment-6676844238).
- [Mai Khắc Hoàng Vũ / UNKN0W-Git](https://github.com/sangcotdo2407/xv6-lab1-group7/pull/4#issuecomment-6677182364).

Đã đọc toàn bộ nội dung hai comment, danh sách reviews, review threads và
discussion timeline. Hiện có hai comment trên Conversation; danh sách review
và inline thread đều rỗng, không có Request Changes chính thức hoặc reply trong
thread bị bỏ sót. Việc đối chiếu không tự gửi reply, đóng thread hoặc thay trạng
thái review.

## Từng nhận xét và nguyên nhân

| Nhận xét | Đánh giá và xử lý |
| --- | --- |
| Thi #1: thống nhất file_index; Vũ P2: extern ftable lặp layout | Hợp lệ về phụ thuộc layout, không phải lỗi tính index hiện tại. Bỏ extern và include thừa ở proc.c; gọi `int file_index_of(struct file *)` đúng chữ ký PR #3. Chỉ file.c kiểm tra địa chỉ số nguyên, biên và căn chỉnh; không dereference hoặc lấy lock. |
| Thi #2; Vũ P3 về collector tạm/xung đột | Hợp lệ về tích hợp. Ghi rõ thứ tự merge và quyền sở hữu bên dưới; kiểm chứng ghép module trong thư mục tạm. Không sửa hoặc rebase các nhánh của người khác. |
| Thi #3: copyout lỗi trên toolchain khác | Chưa thể quy lỗi cho PR. Đối chiếu main gốc với cùng toolchain/ELF; RX mặc định đạt, RWE mô phỏng làm cả main và bản sửa lỗi. Ghi rõ chưa có linux-gnu để kiểm tra trực tiếp. Runner lưu GCC/ld/QEMU/readelf tự động và có `--toolprefix`. |
| Thi #4: exit khóa 16 lần kể cả FD trống | Hợp lệ về chi phí. Đọc FD của owner trước, bỏ qua NULL, chỉ lấy khóa khi tháo FD; vẫn nhả khóa trước fileclose có thể sleep. Reader không thay đổi bảng và xv6 không có thread thứ hai cùng sửa bảng FD của owner. |
| Thi #5, tài liệu cá nhân/quy trình | Xóa đường dẫn máy cá nhân, rút gọn báo cáo kỹ thuật, chuyển phần xử lý review và quy tắc tích hợp sang tài liệu này. Giữ giải thích fork/scheduler/context và các giới hạn cần thiết cho vấn đáp. |
| Thi #5, tests trong patch và chuỗi neo dễ vỡ | Giữ tests trong source patch để reviewer tái chạy; loại log, ảnh build, bytecode bằng gitignore. Chèn test chỉ trong định nghĩa forkret, không neo vào comment hoặc usertrapret. Kiểm tra số định nghĩa/barrier và thứ tự fsinit/first=0; lỗi rõ ràng khi nguồn đổi. Thêm 4 unit test cho instrumentation. Không có hook trong usertrap hoặc kernel production. |
| Thi #6: giới hạn snapshot | Giữ rõ trong báo cáo rút gọn: snapshot không nguyên tử giữa file/process; file index có thể tái sử dụng; wait_lock chỉ giữ theo từng hàng. Không hứa ref luôn bằng tổng FD trên workload đang đổi. |
| Vũ P3: proc.c:718 có dòng trống EOF | Không tái hiện trên PR #4 tại 5e60f38: `git diff --check main...HEAD` đạt và file kết thúc đúng một newline. Dòng 718 cùng cảnh báo này thuộc collector tạm trong PR #2/#3. Không xóa dòng khác một cách máy móc; kiểm tra lại diff hiện tại cũng đạt. |

## Phạm vi bản sửa

`kernel/proc.c`, `kernel/file.c`, `kernel/defs.h`,
`tests/process_info_kernel.c`, `tests/run_process_info.py`,
`tests/test_process_info_runner.py` và ba tài liệu `process-info-*.md`.

Helper ở file.c/defs.h là phụ thuộc tối thiểu được cả hai reviewer đề nghị.
Giữ nguyên tên, tham số, kiểu trả về như PR #3; không tự đặt API mới hoặc đổi
`sysinfo.h`, chữ ký collect_*, syscall, user API hay collector file của Vũ.
`sched()`, `yield()`, `fork()`, `swtch.S` và vị trí tăng/reset counter không đổi.

Kernel selftest kiểm tra mọi slot file 0..99 qua helper, NULL, trước/cuối bảng,
con trỏ lệch 1/8 byte và UINT64_MAX; lỗi collector phải xóa phần process, giữ
nguyên phần file và nhả hết khóa. Test không còn khai báo lại layout ftable.

## Quy tắc tích hợp / nội dung bổ sung cho mô tả PR

Đề xuất văn bản cho phần tích hợp trong mô tả PR #4; chưa tự cập nhật mô tả PR:

> PR #4 cung cấp process collector chính thức, scheduling_count và đồng bộ các
> writer ofile[]. File index dùng chung helper file_index_of() ở file.c với
> kiểm tra miền/căn chỉnh, cùng chữ ký mà PR #3 sử dụng.
>
> Thứ tự đề xuất: review và merge PR #4 trước; PR #2/#3 sau đó rebase trên main
> mới. Khi giải quyết xung đột, giữ đúng một collect_process_info(), một trường
> scheduling_count và một điểm tăng trong scheduler của PR #4; bỏ các bản tạm.
> Giữ đúng một helper đã kiểm tra căn chỉnh; thêm collect_file_info() của Vũ,
> syscall/user test của Thi. Collector file cũng cần dùng helper thay phép trừ
> con trỏ riêng, và syscall cần xử lý -1 theo review ở PR #2/#3.
>
> Selftest, kernel production và bản ghép module đã được kiểm chứng trên QEMU
> 1/3 CPU. Các phần chưa hoàn tất ở PR #2/#3 không được coi là lỗi PR #4 phải tự
> triển khai; xem kết quả và giới hạn trong docs/process-info-tests.md.

Không tự merge để xóa xung đột lịch sử. Merge conflict trên các nhánh hiện tại
vẫn tồn tại cho đến khi thành viên thực hiện rebase/giải quyết theo quy tắc này.

## Kiểm thử sau sửa

- 4 unit test instrumentation: PASS.
- Kernel selftest và production: build PASS; forktest và 60 ca usertests -q
  PASS ở cả 1 và 3 CPU, không kernel panic.
- Bản ghép tạm với PR #3/PR #2: filetest, sysinfotest 84/84 và forktest PASS ở
  1/3 CPU; stress 4 worker, 10.000 snapshot PASS trên 3 CPU.
- Đối chứng copyout: xem bảng ELF/layout trong process-info-tests.md; không sửa
  linker production để che kết quả test.
- Rà lại toàn bộ diff từ main, mọi writer ofile[], các đường lỗi collector,
  khóa parent và counter; git diff --check đạt. Không đổi giao diện sysinfo.

Log cục bộ ở `test-results/pr4-review-fixes/`, không đưa vào source patch.
Linker vẫn cảnh báo RWX cho một số binary xv6; chưa chạy slowtests/stress dài
hạn hoặc toolchain linux-gnu của reviewer. Những lỗi đã ghi ở PR #2/#3 nằm
ngoài phạm vi bản sửa này và vẫn cần chủ phần việc xử lý.

Bản sửa đã kiểm thử và sẵn sàng review lại. Commit/push theo yêu cầu chỉ cập
nhật nhánh feature/process-info; không tự đóng thread hoặc thay trạng thái review.
