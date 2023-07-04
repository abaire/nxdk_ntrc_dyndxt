#include "pushbuffer_command.h"

#include <stdio.h>
#include <string.h>

#include "xbdm.h"
#include "xbox_helper.h"

static const DWORD kTag = 0x6E745043;  // 'ntPC'

DWORD ParsePushBufferCommand(DWORD addr, DWORD command,
                             PushBufferCommand *info) {
  char buf[128];
  int buf_end = sprintf(buf, "0x%08X: Opcode: 0x%08X", addr, command);
  info->valid = FALSE;

  if ((command & 0xE0000003) == 0x20000000) {
    // state->get_jmp_shadow = control->dma_get;
    // NV2A_DPRINTF("pb OLD_JMP 0x%" HWADDR_PRIx "\n", control->dma_get);
    addr = command & 0x1FFFFFFC;
    sprintf(buf + buf_end, "; old jump 0x%08X\n", addr);
    DbgPrint(buf);
    return addr;
  }

  if ((command & 3) == 1) {
    addr = command & 0xFFFFFFFC;
    sprintf(buf + buf_end, "; jump 0x%08X\n", addr);
    DbgPrint(buf);
    // state->get_jmp_shadow = control->dma_get;
    return addr;
  }

  if ((command & 3) == 2) {
    sprintf(buf + buf_end, "; unhandled opcode type: call\n");
    DbgPrint(buf);
    // if (state->subroutine_active) {
    //  state->error = NV_PFIFO_CACHE1_DMA_STATE_ERROR_CALL;
    //  break;
    // }
    // state->subroutine_return = control->dma_get;
    // state->subroutine_active = true;
    // control->dma_get = command & 0xfffffffc;
    return 0;
  }

  if (command == 0x00020000) {
    sprintf(buf + buf_end, "; unhandled opcode type: return\n");
    DbgPrint(buf);
    return 0;
  }

  DWORD masked = command & 0xE0030003;
  BOOL is_method_increasing = !masked;
  BOOL is_method_non_increasing = (masked == 0x40000000);

  if (is_method_increasing || is_method_non_increasing) {
    // Should method be (command >> 2) & 0x7ff?
    // See
    // https://envytools.readthedocs.io/en/latest/hw/fifo/dma-pusher.html//fifo-dma-pusher
    info->valid = TRUE;
    info->method = command & 0x1FFF;
    info->subchannel = (command >> 13) & 7;
    info->parameter_count = (command >> 18) & 0x7FF;
    info->non_increasing = is_method_non_increasing;

    addr += 4 + info->parameter_count * 4;
    return addr;
  }

  sprintf(buf + buf_end, "; unknown opcode type\n");
  DbgPrint(buf);

  return addr;
}

DWORD ParsePushBufferCommandTraceInfo(DWORD pull_addr,
                                      PushBufferCommandTraceInfo *info,
                                      BOOL discard_parameters) {
  info->valid = FALSE;
  info->data = NULL;

  // Retrieve command type from Xbox
  DWORD raw_cmd = ReadDWORD(0x80000000 | pull_addr);
  // self.html_log.log(["", "", "", "@0x%08X: DATA: 0x%08X" % (pull_addr,
  // word)])

  // FIXME: Get where this command ends.
  DWORD next_parser_addr =
      ParsePushBufferCommand(pull_addr, raw_cmd, &info->command);

  if (!next_parser_addr) {
    // If we don't know where this command ends, we have to abort.
    DbgPrint("Failed to process command 0x%08X at 0x%08X\n", raw_cmd,
             pull_addr);
    return 0;
  }

  if (info->command.valid) {
    info->valid = TRUE;
    info->address = pull_addr;

    // TODO: Check to see if it's possible for this to be out of sync.
    // If there's some backup in the pushbuffer processing, is it possible for
    // the CTX_SWITCH1 register to be set for a command other than the one at
    // `pull_addr`?
    info->graphics_class = FetchActiveGraphicsClass();

    // Note: Halo: CE has cases where `parameter_count` == 0 that must be
    // accounted for.
    if (info->command.parameter_count && !discard_parameters) {
      DWORD data_len = info->command.parameter_count * 4;
      info->data = (uint8_t *)DmAllocatePoolWithTag(data_len, kTag);
      if (!info->data) {
        info->valid = FALSE;
        DbgPrint(
            "Allocation failed processing %d data bytes for command 0x%08X at "
            "0x%08X\n",
            data_len, raw_cmd, pull_addr);
        return 0;
      }
      memcpy(info->data, (const uint8_t *)(pull_addr + 4), data_len);
    }
  }

  return next_parser_addr;
}

void DeletePushBufferCommandTraceInfo(PushBufferCommandTraceInfo *info) {
  if (!info->valid || !info->data) {
    return;
  }

  DmFreePool(info->data);
  info->data = NULL;
}
