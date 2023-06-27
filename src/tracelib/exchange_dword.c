#include "exchange_dword.h"

DWORD ExchangeDWORD(intptr_t address, DWORD value) {
  DWORD ret;
  __asm__ volatile(
      ".intel_syntax noprefix\n"
      "cli\n"
      "xchg DWORD PTR [edx], eax\n"
      "sti\n"
      : "=a"(ret)
      : "d"(address), "a"(value));

  return ret;
}