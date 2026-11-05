# Pull Request Review Guidelines – xv6 Lab 1

## 1. Mục đích

Thống nhất quy trình review Pull Request trước khi merge vào `main`, nhằm hạn chế lỗi, xung đột mã nguồn và đảm bảo đáp ứng yêu cầu Lab 1.

## 2. Quy tắc review

- Mỗi PR cần ít nhất **1 Approve** từ thành viên khác.
- Người tạo PR không tự Approve PR của mình.
- Không merge nếu còn lỗi nghiêm trọng hoặc kiểm thử liên quan chưa đạt.
- Các thay đổi liên quan `sysinfo.h`, `defs.h` hoặc giao diện chung cần được cả nhóm xem xét.
- Reviewer phải đọc code và kiểm tra logic, không Approve chỉ vì code biên dịch thành công.
- Sau khi tác giả sửa code theo góp ý, reviewer cần kiểm tra lại commit mới nhất.

## 3. Checklist bắt buộc

### A. Code Quality

- [ ] Code biên dịch được trên xv6-labs-2023.
- [ ] Không có lỗi cú pháp, lỗi con trỏ hoặc truy cập mảng ngoài giới hạn.
- [ ] Không thay đổi những thành phần không liên quan.
- [ ] Code dễ đọc, tên biến và hàm rõ ràng.
- [ ] Không tạo lỗi mới khi sửa các lỗi cũ.

### B. System Call – Sysinfo

- [ ] Thống nhất với `kernel/sysinfo.h`.
- [ ] Tên hàm và tham số đúng theo `kernel/defs.h`.
- [ ] Xử lý tham số không hợp lệ.
- [ ] Dữ liệu trả về User Space chính xác.
- [ ] Không cấp phát cấu trúc `sysinfo` lớn trên kernel stack.

### C. File Management

- [ ] `file_index` phản ánh đúng vị trí trong `ftable.file[]`.
- [ ] Đếm chính xác các đối tượng file có `ref > 0`.
- [ ] Reference count chính xác khi dùng `open()`, `dup()`, `fork()`, `close()`.
- [ ] Danh sách PID không bị trùng.
- [ ] Quản lý khóa `ftable.lock` và `p->lock` an toàn.

### D. Process Management & Scheduling

- [ ] Thống kê đúng các tiến trình đang tồn tại.
- [ ] PID và Parent PID chính xác.
- [ ] Trạng thái tiến trình phù hợp với `proc.h`.
- [ ] `scheduling_count` được cập nhật đúng lúc.
- [ ] Không làm hỏng cơ chế `fork()`, `scheduler()`, `sched()`, `yield()`.

### E. Testing

- [ ] Có bằng chứng kiểm thử liên quan.
- [ ] Kiểm thử cả trường hợp bình thường và trường hợp lỗi.
- [ ] Không làm hỏng chức năng xv6 có sẵn.
- [ ] Không còn lỗi nghiêm trọng chưa xử lý.

## 4. Kết quả review

**Approve:** Code đáp ứng yêu cầu, không còn lỗi nghiêm trọng và đã kiểm thử phù hợp.

**Request Changes:** Có lỗi cần sửa trước khi merge.

**Comment:** Cần làm rõ hoặc đề xuất cải thiện, chưa đưa ra quyết định Approve.

## 5. Quy trình

1. Tác giả tạo PR vào `main`.
2. Reviewer đọc mô tả PR và đối chiếu Issue.
3. Kiểm tra các file thay đổi và logic liên quan.
4. Kiểm thử nếu có điều kiện.
5. Gửi nhận xét hoặc Request Changes.
6. Tác giả sửa code và push commit mới.
7. Reviewer kiểm tra lại thay đổi mới và các chức năng liên quan.
8. Chỉ merge khi đạt yêu cầu và có ít nhất 1 Approve hợp lệ.

## 6. Nguyên tắc quan trọng

**Một PR được Approve không đồng nghĩa với code chắc chắn không có lỗi.**

Review phải tập trung vào tính đúng đắn, an toàn bộ nhớ, khóa đồng bộ và khả năng tích hợp của cả Lab 1.

Nếu chưa thể kiểm thử, reviewer phải nêu rõ giới hạn kiểm tra thay vì khẳng định code đã hoạt động đúng.