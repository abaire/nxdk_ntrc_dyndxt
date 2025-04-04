#include "kick_fifo.h"

#include "register_defs.h"
#include "xbox_helper.h"

#define LOOP_CYCLES 128

KickResult KickFIFO(uint32_t expected_push) {
  KickResult ret = KICK_BAD_READ_PUSH_ADDR;

  // Avoid any other CPU stuff overwriting stuff in this risky section
  __asm__ volatile(
      ".intel_syntax noprefix\n"
      "cli\n");

  if (expected_push != GetDMAPushAddress()) {
    goto done;
  }

  uint32_t i = 0;
  ResumeFIFOPusher();

  for (; i < LOOP_CYCLES; ++i) {
    uint32_t state = ReadDWORD(CACHE_PUSH_STATE);
    if (state & NV_PFIFO_CACHE1_DMA_PUSH_BUFFER) {
      ret = KICK_OK;
      break;
    }
    SwitchToThread();
  }

  PauseFIFOPusher();

  if (i >= LOOP_CYCLES) {
    ret = KICK_TIMEOUT;
  }

  if (expected_push != GetDMAPushAddress()) {
    ret = KICK_PUSH_MODIFIED_IN_CALL;
  }

done:
  __asm__ volatile(
      ".intel_syntax noprefix\n"
      "sti\n");
  return ret;
}
