#include "kick_fifo.h"

#include "register_defs.h"
#include "xbox_helper.h"

#define BUSY_LOOP_CYCLES 0x2000

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

  // Do a short busy loop. Ideally this would wait forever until the push buffer
  // becomes empty, but it is assumed that the caller may want to provide some
  // timeout mechanism.
  for (; i < BUSY_LOOP_CYCLES; ++i) {
    uint32_t state = ReadDWORD(CACHE_PUSH_STATE);
    if (!(state & NV_PFIFO_CACHE1_DMA_PUSH_BUFFER)) {
      ret = KICK_OK;
      break;
    }
  }

  if (i >= BUSY_LOOP_CYCLES) {
    ret = KICK_TIMEOUT;
  }

  PauseFIFOPusher();

  if (expected_push != GetDMAPushAddress()) {
    ret = KICK_PUSH_MODIFIED_IN_CALL;
  }

done:
  __asm__ volatile(
      ".intel_syntax noprefix\n"
      "sti\n");
  return ret;
}
