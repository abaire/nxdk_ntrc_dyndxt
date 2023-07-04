#include "exchange_dword.h"

uint32_t ExchangeDWORD(intptr_t address, uint32_t value) {
  uint32_t ret;
  __asm__ volatile(
      ".intel_syntax noprefix\n"
      "cli\n"
      "xchg DWORD PTR [edx], eax\n"
      "sti\n"
      : "=a"(ret)
      : "d"(address), "a"(value));

  return ret;
}
