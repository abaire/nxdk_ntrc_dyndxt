#include "xbox_helper.h"

#include "register_defs.h"

DWORD ReadDWORD(intptr_t address) { return *(volatile DWORD *)(address); }

void WriteDWORD(intptr_t address, DWORD value) {
  *(volatile DWORD *)(address) = value;
}

void DisablePGRAPHFIFO(void) {
  DWORD state = ReadDWORD(PGRAPH_STATE);
  WriteDWORD(PGRAPH_STATE, state & 0xFFFFFFFE);
}

void EnablePGRAPHFIFO(void) {
  DWORD state = ReadDWORD(PGRAPH_STATE);
  WriteDWORD(PGRAPH_STATE, state | 0x00000001);
}

void BusyWaitUntilPGRAPHIdle(void) {
  while (ReadDWORD(PGRAPH_STATUS) & 0x00000001) {
  }
}

void PauseFIFOPuller(void) {
  DWORD state = ReadDWORD(CACHE_PULL_STATE);
  WriteDWORD(CACHE_PULL_STATE, state & 0xFFFFFFFE);
}

void ResumeFIFOPuller(void) {
  DWORD state = ReadDWORD(CACHE_PULL_STATE);
  WriteDWORD(CACHE_PULL_STATE, state | 0x00000001);
}

void PauseFIFOPusher(void) {
  DWORD state = ReadDWORD(CACHE_PUSH_STATE);
  WriteDWORD(CACHE_PUSH_STATE, state & 0xFFFFFFFE);
}

void ResumeFIFOPusher(void) {
  DWORD state = ReadDWORD(CACHE_PUSH_STATE);
  WriteDWORD(CACHE_PUSH_STATE, state | 0x00000001);
}

void BusyWaitUntilPusherIDLE(void) {
  const DWORD busy_bit = 1 << 4;
  while (ReadDWORD(CACHE_PUSH_STATE) & busy_bit) {
  }
}

void MaybePopulateFIFOCache(void) {
  ResumeFIFOPusher();
  PauseFIFOPusher();
}

DWORD GetDMAPushAddress(void) { return ReadDWORD(DMA_PUSH_ADDR); }

DWORD GetDMAPullAddress(void) { return ReadDWORD(DMA_PULL_ADDR); }

void SetDMAPushAddress(DWORD target) { WriteDWORD(DMA_PUSH_ADDR, target); }

void GetDMAState(DMAState *state) {
  DWORD dma_state = ReadDWORD(DMA_STATE);

  state->non_increasing = (dma_state & 0x01) != 0;
  state->method = (dma_state >> 2) & 0x1FFF;
  state->subchannel = (dma_state >> 13) & 0x07;
  state->method_count = (dma_state >> 18) & 0x7FF;
  state->error = (dma_state >> 29) & 0x07;
}

DWORD FetchActiveGraphicsClass(void) {
  DWORD ctx_switch_1 = ReadDWORD(CTX_SWITCH1);
  return ctx_switch_1 & 0xFF;
}
