#ifndef NXDK_NTRC_DYNDXT_CIRCULAR_BUFFER_H_
#define NXDK_NTRC_DYNDXT_CIRCULAR_BUFFER_H_

// Provides circular buffer functionality.
//
// No concurrency protection is provided.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *CircularBuffer;

typedef void *(*CBAllocProc)(size_t);
typedef void (*CBFreeProc)(void *);

// Creates a new circular buffer with the given capacity using the default
// malloc/free.
CircularBuffer CBCreate(uint32_t size);

// Creates a new circular buffer using the given memory.
// The given allocator and free methods are used to create/destroy the buffer
// and its contents.
CircularBuffer CBCreateEx(uint32_t size, CBAllocProc alloc_proc,
                          CBFreeProc free_proc);

// Destroys the given circular buffer and frees allocated resources.
void CBDestroy(CircularBuffer handle);

// Returns the maximum size of the given buffer.
uint32_t CBCapacity(CircularBuffer handle);

// Returns the number of bytes available for reading.
uint32_t CBAvailable(CircularBuffer handle);

// Returns the number of bytes that may be written before the buffer is full.
uint32_t CBFreeSpace(CircularBuffer handle);

// Discards up to the given number of bytes.
// Returns the actual number of bytes discarded.
uint32_t CBDiscard(CircularBuffer handle, uint32_t bytes);

// Empties the buffer.
void CBClear(CircularBuffer handle);

// Attempts to write all of the given data to the buffer.
// Returns true if the data was written successfully.
bool CBWrite(CircularBuffer handle, const void *data, uint32_t data_size);

// Writes up to `max_size` bytes to the buffer.
// Returns the actual number of bytes written.
uint32_t CBWriteAvailable(CircularBuffer handle, const void *data,
                          uint32_t max_size);

// Attempts to read exactly `size` bytes from the buffer.
// Returns true if the data was read successfully.
bool CBRead(CircularBuffer handle, void *buffer, uint32_t size);

// Reads up to `max_size` bytes from the buffer.
// Returns the actual number of bytes read.
uint32_t CBReadAvailable(CircularBuffer handle, void *buffer,
                         uint32_t max_size);

#ifdef __cplusplus
};  // extern "C"
#endif

#endif  // NXDK_NTRC_DYNDXT_CIRCULAR_BUFFER_H_
