#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

#define Reg(reg) ((volatile unsigned char *)(UART0 + reg))

#define RHR 0
#define THR 0
#define IER 1
#define IER_RX_ENABLE (1<<0)
#define IER_TX_ENABLE (1<<1)
#define FCR 2
#define FCR_FIFO_ENABLE (1<<0)
#define FCR_FIFO_CLEAR (3<<1)
#define ISR 2
#define LCR 3
#define LCR_EIGHT_BITS (3<<0)
#define LCR_BAUD_LATCH (1<<7)
#define LSR 5
#define LSR_RX_READY (1<<0)
#define LSR_TX_IDLE (1<<5)

#define ReadReg(reg) (*(Reg(reg)))
#define WriteReg(reg, v) (*(Reg(reg)) = (v))

struct spinlock uart_tx_lock;
#define UART_TX_BUF_SIZE 32
char uart_tx_buf[UART_TX_BUF_SIZE];
uint64 uart_tx_w;
uint64 uart_tx_r;

extern volatile int panicked;

void uartstart();

void
uartinit(void)
{

  WriteReg(IER, 0x00);

  WriteReg(LCR, LCR_BAUD_LATCH);

  WriteReg(0, 0x03);

  WriteReg(1, 0x00);

  WriteReg(LCR, LCR_EIGHT_BITS);

  WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);

  WriteReg(IER, IER_TX_ENABLE | IER_RX_ENABLE);

  initlock(&uart_tx_lock, "uart");
}

void
uartputc(int c)
{
  acquire(&uart_tx_lock);

  if(panicked){
    for(;;)
      ;
  }
  while(uart_tx_w == uart_tx_r + UART_TX_BUF_SIZE){

    sleep(&uart_tx_r, &uart_tx_lock);
  }
  uart_tx_buf[uart_tx_w % UART_TX_BUF_SIZE] = c;
  uart_tx_w += 1;
  uartstart();
  release(&uart_tx_lock);
}

void
uartputc_sync(int c)
{
  push_off();

  if(panicked){
    for(;;)
      ;
  }

  while((ReadReg(LSR) & LSR_TX_IDLE) == 0)
    ;
  WriteReg(THR, c);

  pop_off();
}

void
uartstart()
{
  while(1){
    if(uart_tx_w == uart_tx_r){

      return;
    }

    if((ReadReg(LSR) & LSR_TX_IDLE) == 0){

      return;
    }

    int c = uart_tx_buf[uart_tx_r % UART_TX_BUF_SIZE];
    uart_tx_r += 1;

    wakeup(&uart_tx_r);

    WriteReg(THR, c);
  }
}

int
uartgetc(void)
{
  if(ReadReg(LSR) & 0x01){

    return ReadReg(RHR);
  } else {
    return -1;
  }
}

void
uartintr(void)
{

  while(1){
    int c = uartgetc();
    if(c == -1)
      break;
    consoleintr(c);
  }

  acquire(&uart_tx_lock);
  uartstart();
  release(&uart_tx_lock);
}
