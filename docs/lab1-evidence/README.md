# Bằng chứng kiểm thử tích hợp

Thư mục này giữ bằng chứng phiên tích hợp trước khi rút gọn test/bỏ comment.
Kết quả của cây hiện tại nằm ở [bản cập nhật](../lab1-test-simplification.md).

Log từ các lượt chạy ngày 07/11/2026, chỉ chuẩn hóa LF và bỏ khoảng trắng
cuối dòng để git diff --check không báo dấu nhắc shell/dòng lệnh make.
Log thô giữ nguyên trong test-results/lab1-integration. Code được kiểm thử là
commit `6c57aac` (các lượt suite trước đó chỉ khác comment/whitespace).
Manifest ghi SHA-256 của cả log thô và bản lưu trong Git, commit code, main
và ba head PR; source_sha256_lf cho phép đối chiếu nguồn đã chuẩn hóa LF.
extra-summary.json còn ghi hash từng file kernel/user tại lúc clean build;
đã đối chiếu trùng với nguồn cuối trước khi commit tài liệu.

| File | Ý nghĩa |
| --- | --- |
| production-cpu1.log / production-cpu3.log | Kernel production: filetest 49 check, sysinfotest 102/102, forktest và 60 usertests. |
| process-selftest-cpu1.log / process-selftest-cpu3.log | Kernel được gắn selftest trong bản sao: 32 dispatch chính xác, lifecycle/ref/bounds, forktest và 60 usertests. |
| file-probe-cpu1.log / file-probe-cpu3.log | Con trỏ kernel lệch, giới hạn, cleanup, canary và boot tiếp. |
| make-qemu-cpu1.log / make-qemu-cpu3.log | make qemu thực tế; stressfs và xác minh năm file, rồi sysinfotest 102/102. |
| collector-fault-cpu1.log / collector-fault-cpu3.log | Hai lỗi collector, không copyout; 100 lần gọi sau đó phục hồi. |
| metadata-before-fix.log | Regression FAIL được ghi trước sửa; không phải kết quả cây cuối. |
| native-file-after-fix.log | Regression metadata, helper/collector trên code thật, ASan/UBSan PASS. |
| native-uint64.log | In 0, 2^31, 2^32, UINT64_MAX bằng formatter thật, ASan/UBSan PASS. |
| clean-build-production.log / clean-build-fault.log | make clean và build, bao gồm cảnh báo linker có sẵn. |
| sysinfotest-elf.txt | ELF text R E, data RW của chương trình test. |
| environment.json / extra-summary.json / manifest.json | Toolchain, ma trận kết quả và định danh nguồn/bằng chứng. |

Lệnh tái hiện và phân tích kết quả nằm trong
[báo cáo tích hợp](../lab1-integration-review.md). Bản build có fault/selftest
chỉ tồn tại trong thư mục tạm của runner; không thay Makefile hoặc kernel
production để chèn chúng. Không đưa binary hay filesystem image vào Git.

usertests có các usertrap cố ý kiểm địa chỉ sai, kết thúc bằng OK;
đây không phải kernel panic. Không có panic trong các log đã commit.
Các log lịch sử của từng PR không được dùng thay bằng chứng của cây tích hợp.
