#ifndef NXDK_NTRC_DYNDXT_CIRCULAR_BUFFER_IMPL_H_
#define NXDK_NTRC_DYNDXT_CIRCULAR_BUFFER_IMPL_H_

typedef struct CircularBufferImpl {
  uint8_t *buffer;
  size_t size;

  size_t read;
  size_t write;

  CBFreeProc free_proc;
} CircularBufferImpl;

#endif  // NXDK_NTRC_DYNDXT_CIRCULAR_BUFFER_IMPL_H_
