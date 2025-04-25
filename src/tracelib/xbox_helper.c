#include "xbox_helper.h"

#include "register_defs.h"

uint32_t ReadDWORD(intptr_t address) { return *(volatile uint32_t *)(address); }

void WriteDWORD(intptr_t address, uint32_t value) {
  *(volatile uint32_t *)(address) = value;
}

void DisablePGRAPHFIFO(void) {
  uint32_t state = ReadDWORD(PGRAPH_STATE);
  WriteDWORD(PGRAPH_STATE, state & 0xFFFFFFFE);
}

void EnablePGRAPHFIFO(void) {
  uint32_t state = ReadDWORD(PGRAPH_STATE);
  WriteDWORD(PGRAPH_STATE, state | 0x00000001);
}

void BusyWaitUntilPGRAPHIdle(void) {
  while (ReadDWORD(PGRAPH_STATUS) & 0x00000001) {
  }
}

void PauseFIFOPuller(void) {
  uint32_t state = ReadDWORD(CACHE_PULL_STATE);
  WriteDWORD(CACHE_PULL_STATE, state & 0xFFFFFFFE);
}

void ResumeFIFOPuller(void) {
  uint32_t state = ReadDWORD(CACHE_PULL_STATE);
  WriteDWORD(CACHE_PULL_STATE, state | 0x00000001);
}

void PauseFIFOPusher(void) {
  uint32_t state = ReadDWORD(CACHE_PUSH_STATE);
  WriteDWORD(CACHE_PUSH_STATE, state & 0xFFFFFFFE);
}

void ResumeFIFOPusher(void) {
  uint32_t state = ReadDWORD(CACHE_PUSH_STATE);
  WriteDWORD(CACHE_PUSH_STATE, state | 0x00000001);
}

void BusyWaitUntilPusherIDLE(void) {
  const uint32_t busy_bit = 1 << 4;
  while (ReadDWORD(CACHE_PUSH_STATE) & busy_bit) {
  }
}

void MaybePopulateFIFOCache(uint32_t sleep_milliseconds) {
  ResumeFIFOPusher();
  if (sleep_milliseconds) {
    Sleep(sleep_milliseconds);
  }
  PauseFIFOPusher();
}

uint32_t GetDMAPushAddress(void) { return ReadDWORD(DMA_PUSH_ADDR); }

uint32_t GetDMAPullAddress(void) { return ReadDWORD(DMA_PULL_ADDR); }

void SetDMAPushAddress(uint32_t target) { WriteDWORD(DMA_PUSH_ADDR, target); }

void GetDMAState(DMAState *state) {
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
