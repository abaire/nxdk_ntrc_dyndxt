#ifndef NXDK_NTRC_DYNDXT_CIRCULAR_BUFFER_IMPL_H_
#define NXDK_NTRC_DYNDXT_CIRCULAR_BUFFER_IMPL_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CircularBufferImpl {
  uint8_t *buffer;
  size_t size;

  size_t read;
  size_t write;

  CBFreeProc free_proc;
} CircularBufferImpl;

#ifdef __cplusplus
}  //  extern "C"
#endif

#endif  // NXDK_NTRC_DYNDXT_CIRCULAR_BUFFER_IMPL_H_
