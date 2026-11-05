
// 
// Các hàm hỗ trợ cho những lời gọi hệ thống liên quan đến file descriptor.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"
#include "sysinfo.h"

extern struct proc proc[NPROC];   // Bảng quản lý tiến trình, được định nghĩa trong proc.c

struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// Cấp phát một cấu trúc file mới.
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// Tăng bộ đếm tham chiếu (ref) của file f lên 1.
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// Đóng file f (giảm bộ đếm tham chiếu, giải phóng file khi ref bằng 0).
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if(ff.type == FD_PIPE){
    pipeclose(ff.pipe, ff.writable);
  } else if(ff.type == FD_INODE || ff.type == FD_DEVICE){
    begin_op();
    iput(ff.ip);
    end_op();
  }
}

// Lấy thông tin metadata của file f.
// addr là địa chỉ ảo trong không gian người dùng, trỏ đến một cấu trúc struct stat.
int
filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();
  struct stat st;
  
  if(f->type == FD_INODE || f->type == FD_DEVICE){
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);
    if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

// Đọc dữ liệu từ file f.
// addr là địa chỉ ảo trong không gian người dùng.
int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if(f->readable == 0)
    return -1;

  if(f->type == FD_PIPE){
    r = piperead(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(1, addr, n);
  } else if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
  } else {
    panic("fileread");
  }

  return r;
}

// Ghi dữ liệu vào file f.
// addr là địa chỉ ảo trong không gian người dùng.
int
filewrite(struct file *f, uint64 addr, int n)
{
  int r, ret = 0;

  if(f->writable == 0)
    return -1;

  if(f->type == FD_PIPE){
    ret = pipewrite(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(1, addr, n);
  } else if(f->type == FD_INODE){
    // Ghi từng nhóm block để tránh vượt quá kích thước giao dịch log tối đa,
    // bao gồm i-node, block gián tiếp, các block cấp phát
    // và 2 block dự phòng cho những lần ghi không căn chỉnh.
    // Phần xử lý này thực ra nên được đặt ở tầng thấp hơn,
    // vì writei() cũng có thể đang ghi vào một thiết bị như console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r != n1){
        // Có lỗi xảy ra trong writei().
        break;
      }
      i += r;
    }
    ret = (i == n ? n : -1);
  } else {
    panic("filewrite");
  }

  return ret;
}


// Thu thập thông tin của tất cả đối tượng file đang mở (ftable.file[i].ref > 0),
// lưu số lượng vào info->open_file_count và thông tin chi tiết vào info->files[].
// Đồng thời thu thập danh sách các PID khác nhau đang tham chiếu đến mỗi đối tượng
// thông qua một vị trí bất kỳ trong mảng ofile[].
//
// info được cung cấp bởi sys_sysinfo() và không bị thay đổi ở nơi khác.
// Biến này không được đặt trên kernel stack.
// Chỉ phần thông tin về file của *info được ghi.
//
// Cơ chế khóa: không bao giờ giữ đồng thời khóa ftable và khóa của một tiến trình.
//   Giai đoạn 1: giữ ftable.lock, sao chép thông tin các đối tượng file, sau đó nhả khóa.
//   Giai đoạn 2: với mỗi tiến trình không ở trạng thái UNUSED, chỉ giữ p->lock
//                và duyệt qua mảng p->ofile[].
//
// Snapshot không hoàn toàn đồng bộ tại một thời điểm:
// một file được mở hoặc đóng giữa hai giai đoạn có thể bị bỏ sót,
// hoặc danh sách PID có thể không khớp với giá trị ref.
//
// Trả về 0 nếu thành công, -1 nếu thất bại.
int
collect_file_info(struct sysinfo *info)
{
  int slot_of[NFILE];   // Ánh xạ chỉ số trong ftable sang vị trí trong info->files[], hoặc -1
  int i, n;
  struct proc *p;

  if(info == 0)
    return -1;

  info->open_file_count = 0;
  memset(info->files, 0, sizeof(info->files));
  for(i = 0; i < NFILE; i++)
    slot_of[i] = -1;

  // Giai đoạn 1: chụp trạng thái các đối tượng file.
  n = 0;
  acquire(&ftable.lock);
  for(i = 0; i < NFILE; i++){
    struct file *f = &ftable.file[i];
    if(f->ref <= 0)
      continue;
    struct sysinfo_file *sf = &info->files[n];
    sf->file_index = i;
    sf->type = f->type;
    sf->readable = f->readable;
    sf->writable = f->writable;
    sf->ref = f->ref;
    sf->process_count = 0;
    slot_of[i] = n;
    n++;
  }
  release(&ftable.lock);
  info->open_file_count = n;

  // Giai đoạn 2: thu thập các PID khác nhau đang tham chiếu đến từng đối tượng file.
  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->state == UNUSED){
      release(&p->lock);
      continue;
    }
    int pid = p->pid;
    for(i = 0; i < NOFILE; i++){
      struct file *f = p->ofile[i];
      if(f < ftable.file || f >= ftable.file + NFILE)
        continue;                       // Con trỏ null hoặc không thuộc bảng ftable.
      int slot = slot_of[f - ftable.file];
      if(slot < 0)
        continue;                       // File được mở sau thời điểm chụp snapshot.
      struct sysinfo_file *sf = &info->files[slot];

      // Tất cả fd của một tiến trình được duyệt liên tiếp,
      // vì vậy chỉ cần so sánh với PID được lưu gần nhất
      // là đủ để đảm bảo mỗi PID chỉ xuất hiện một lần.
      if(sf->process_count > 0 && sf->pids[sf->process_count - 1] == pid)
        continue;
      if(sf->process_count < NPROC)
        sf->pids[sf->process_count++] = pid;
    }
    release(&p->lock);
  }
  return 0;
}
