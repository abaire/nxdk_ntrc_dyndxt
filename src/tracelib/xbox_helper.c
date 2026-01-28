#include "xbox_helper.h"

#include <windows.h>

#include "register_defs.h"

uint32_t ReadDWORD(intptr_t address) { return *(volatile uint32_t*)(address); }

void WriteDWORD(intptr_t address, uint32_t value) {
  *(volatile uint32_t*)(address) = value;
}

void DisablePGRAPHFIFO(void) {
  uint32_t state = ReadDWORD(PGRAPH_FIFO_STATE);
  WriteDWORD(PGRAPH_FIFO_STATE, state & ~NV_PGRAPH_FIFO_ACCESS);
}

void EnablePGRAPHFIFO(void) {
  uint32_t state = ReadDWORD(PGRAPH_FIFO_STATE);
  WriteDWORD(PGRAPH_FIFO_STATE, state | NV_PGRAPH_FIFO_ACCESS);
}

void BusyWaitUntilPGRAPHIdle(void) {
  while (ReadDWORD(PGRAPH_STATUS) & NV_PGRAPH_STATUS_BUSY) {
    __asm__ __volatile__("pause");
  }
}

bool BusyWaitUntilPGRAPHIdleWithTimeout(uint32_t timeout_milliseconds) {
  uint32_t start_time = GetTickCount();
  uint32_t total_time = 0;

  do {
    if (!(ReadDWORD(PGRAPH_STATUS) & NV_PGRAPH_STATUS_BUSY)) {
      return true;
    }

    uint32_t now = GetTickCount();
    if (now < start_time) {
      total_time = now + 0xFFFFFFFF - start_time;
    } else {
      total_time = now - start_time;
    }
    __asm__ __volatile__("pause");
  } while (total_time < timeout_milliseconds);

  return false;
}

void PauseFIFOPuller(void) {
  uint32_t state = ReadDWORD(CACHE1_PULL0);
  WriteDWORD(CACHE1_PULL0, state & ~NV_PFIFO_CACHE1_PUSH0_ACCESS);
}

void ResumeFIFOPuller(void) {
  uint32_t state = ReadDWORD(CACHE1_PULL0);
  WriteDWORD(CACHE1_PULL0, state | NV_PFIFO_CACHE1_PUSH0_ACCESS);
}

void PauseFIFOPusher(void) {
  uint32_t state = ReadDWORD(CACHE1_DMA_PUSH);
  WriteDWORD(CACHE1_DMA_PUSH, state & ~NV_PFIFO_CACHE1_DMA_PUSH_ACCESS);
}

void ResumeFIFOPusher(void) {
  uint32_t state = ReadDWORD(CACHE1_DMA_PUSH);
  WriteDWORD(CACHE1_DMA_PUSH, state | NV_PFIFO_CACHE1_DMA_PUSH_ACCESS);
}

void BusyWaitUntilPusherIDLE(void) {
  while (ReadDWORD(CACHE1_DMA_PUSH) & NV_PFIFO_CACHE1_DMA_PUSH_STATE) {
    __asm__ __volatile__("pause");
  }
}

void MaybePopulateFIFOCache(uint32_t sleep_milliseconds) {
  ResumeFIFOPusher();
  if (sleep_milliseconds) {
    Sleep(sleep_milliseconds);
  }
  PauseFIFOPusher();
}

uint32_t GetDMAPushAddress(void) { return ReadDWORD(DMA_PUT_ADDR); }

uint32_t GetDMAPullAddress(void) { return ReadDWORD(DMA_GET_ADDR); }

void SetDMAPushAddress(uint32_t target) { WriteDWORD(DMA_PUT_ADDR, target); }

void GetDMAState(DMAState* state) {
  uint32_t dma_state = ReadDWORD(DMA_STATE);

  state->non_increasing = (dma_state & 0x01) != 0;
  state->method = (dma_state >> 2) & 0x1FFF;
  state->subchannel = (dma_state >> 13) & 0x07;
  state->method_count = (dma_state >> 18) & 0x7FF;
  state->error = (dma_state >> 29) & 0x07;
}

uint32_t FetchGraphicsClassForSubchannel(uint32_t subchannel) {
  uint32_t data = ReadDWORD(PGRAPH_CTX_CACHE1(subchannel));
  return data & 0xFF;
}
