#include "circular_buffer.h"

#include <stdlib.h>
#include <string.h>

#include "circular_buffer_impl.h"
#include "fastmemcpy/fastmemcpy.h"

static void Write(CircularBufferImpl *cb, const void *data, uint32_t data_size);
static void Read(CircularBufferImpl *cb, void *buffer, uint32_t size);

// Creates a new circular buffer with the given capacity.
CircularBuffer CBCreate(uint32_t size) {
  return CBCreateEx(size, malloc, free);
}

CircularBuffer CBCreateEx(uint32_t size, CBAllocProc alloc_proc,
                          CBFreeProc free_proc) {
  if (!size) {
    return NULL;
  }

  CircularBufferImpl *ret =
      (CircularBufferImpl *)alloc_proc(sizeof(CircularBufferImpl));
  if (!ret) {
    return ret;
  }

  ret->buffer = (uint8_t *)(alloc_proc(size + 1));
  if (!ret->buffer) {
    free_proc(ret);
    return NULL;
  }

  ret->size = size + 1;
  ret->read = 0;
  ret->write = 0;
  ret->free_proc = free_proc;

  return ret;
}

void CBDestroy(CircularBuffer handle) {
  if (!handle) {
    return;
  }
  CircularBufferImpl *cb = (CircularBufferImpl *)handle;
  cb->free_proc(cb->buffer);
  cb->free_proc(cb);
}

uint32_t CBCapacity(CircularBuffer handle) {
  CircularBufferImpl *cb = (CircularBufferImpl *)handle;
  if (!cb) {
    return 0;
  }

  return cb->size - 1;
}

uint32_t CBAvailable(CircularBuffer handle) {
  CircularBufferImpl *cb = (CircularBufferImpl *)handle;
  if (!cb) {
    return 0;
  }

  if (cb->write >= cb->read) {
    return cb->write - cb->read;
  }
  return cb->size + cb->write - cb->read;
}

uint32_t CBFreeSpace(CircularBuffer handle) {
  CircularBufferImpl *cb = (CircularBufferImpl *)handle;
  if (!cb) {
    return 0;
  }

  if (cb->read > cb->write) {
    return (cb->read - 1) - cb->write;
  }

  return (cb->size - 1) - (cb->write - cb->read);
}

uint32_t CBDiscard(CircularBuffer handle, uint32_t bytes) {
  CircularBufferImpl *cb = (CircularBufferImpl *)handle;
  if (!cb) {
    return 0;
  }

  uint32_t available = CBAvailable(handle);
  if (available < bytes) {
    bytes = available;
  }

  cb->read = (cb->read + bytes) % cb->size;
  return bytes;
}

void CBClear(CircularBuffer handle) {
  CircularBufferImpl *cb = (CircularBufferImpl *)handle;
  if (cb) {
    cb->read = cb->write;
  }
}

bool CBWrite(CircularBuffer handle, const void *data, uint32_t data_size) {
  uint32_t free_space = CBFreeSpace(handle);
  if (free_space < data_size) {
    return false;
  }
  Write((CircularBufferImpl *)handle, data, data_size);
  return true;
}

uint32_t CBWriteAvailable(CircularBuffer handle, const void *data,
                          uint32_t max_size) {
  CircularBufferImpl *cb = (CircularBufferImpl *)handle;
  if (!max_size || !cb) {
    return 0;
  }
  uint32_t free_space = CBFreeSpace(handle);
  if (free_space < max_size) {
    max_size = free_space;
  }
  if (max_size) {
    Write(cb, data, max_size);
  }
  return max_size;
}

uint32_t CBReadAvailable(CircularBuffer handle, void *buffer,
                         uint32_t max_size) {
  CircularBufferImpl *cb = (CircularBufferImpl *)handle;
  if (!max_size || !cb) {
    return 0;
  }
  uint32_t available = CBAvailable(handle);
  if (available < max_size) {
    max_size = available;
  }
  Read(cb, buffer, max_size);
  return max_size;
}

bool CBRead(CircularBuffer handle, void *buffer, uint32_t size) {
  if (!size || CBAvailable(handle) < size) {
    return false;
  }
  Read((CircularBufferImpl *)handle, buffer, size);
  return true;
}

static void Write(CircularBufferImpl *cb, const void *data,
                  uint32_t data_size) {
  // data_size will have already been adjusted if the read pointer is ahead of
  // the write, so the only test is against the underlying buffer end.
  uint32_t bytes_to_end = cb->size - cb->write;
  if (bytes_to_end < data_size) {
    mmx_memcpy(cb->buffer + cb->write, data, bytes_to_end);
    data += bytes_to_end;
    cb->write = 0;
    data_size -= bytes_to_end;
  }

  if (data_size) {
    mmx_memcpy(cb->buffer + cb->write, data, data_size);
    cb->write += data_size;
  }
}

static void Read(CircularBufferImpl *cb, void *buffer, uint32_t size) {
  // size will have already been adjusted if the write pointer is ahead of the
  // read, so the only test is against the underlying buffer end.
  uint32_t bytes_to_end = cb->size - cb->read;
  if (bytes_to_end < size) {
    mmx_memcpy(buffer, cb->buffer + cb->read, bytes_to_end);
    buffer += bytes_to_end;
    cb->read = 0;
    size -= bytes_to_end;
  }

  mmx_memcpy(buffer, cb->buffer + cb->read, size);
  cb->read += size;
}
