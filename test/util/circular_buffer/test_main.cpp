#define BOOST_TEST_MODULE CircularBufferTests

#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <set>
#include <string>

#include "util/circular_buffer.h"
static uint8_t test_buffer[1024] = {0};

static size_t total_bytes_allocated = 0;
static bool free_called = false;
static std::set<void *> allocations;

static void *AllocProc(size_t sz) {
  auto new_head = total_bytes_allocated + sz;
  if (new_head >= sizeof(test_buffer)) {
    return nullptr;
  }

  void *ret = test_buffer + total_bytes_allocated;
  allocations.insert(ret);
  total_bytes_allocated = new_head;
  return ret;
}

static void FreeProc(void *buf) {
  auto it = allocations.find(buf);
  BOOST_REQUIRE(it != allocations.end());
  allocations.erase(it);
  free_called = true;
}

static void PopulateBuffer(uint8_t *buf, size_t len) {
  for (auto i = 0; i < len; ++i) {
    buf[i] = i & 0xFF;
  }
}

struct Fixture {
  Fixture() {
    memset(test_buffer, 0, sizeof(test_buffer));
    total_bytes_allocated = 0;
    free_called = false;
    allocations.clear();
  }
};

BOOST_FIXTURE_TEST_SUITE(circular_buffer_suite, Fixture)

BOOST_AUTO_TEST_CASE(zero_size_returns_null) {
  auto sut = CBCreate(0);

  BOOST_TEST(sut == nullptr);
  BOOST_TEST(allocations.empty());
}

BOOST_AUTO_TEST_CASE(too_large_allocation_returns_null) {
  auto sut = CBCreateEx(sizeof(test_buffer) + 1, AllocProc, FreeProc);

  BOOST_TEST(sut == nullptr);
  BOOST_TEST(allocations.empty());
}

BOOST_AUTO_TEST_CASE(alloc_may_be_provided) {
  auto sut = CBCreateEx(64, AllocProc, FreeProc);

  BOOST_TEST(sut != nullptr);
  BOOST_TEST(allocations.size() > 0);
}

BOOST_AUTO_TEST_CASE(free_may_be_provided) {
  auto sut = CBCreateEx(64, AllocProc, FreeProc);

  CBDestroy(sut);

  BOOST_TEST(allocations.empty());
  BOOST_TEST(free_called == true);
}

BOOST_AUTO_TEST_CASE(capacity_returns_total_size) {
  auto sut = CBCreateEx(64, AllocProc, FreeProc);

  auto value = CBCapacity(sut);

  BOOST_TEST(value == 64);
}

BOOST_AUTO_TEST_CASE(available_when_empty_returns_zero) {
  auto sut = CBCreateEx(64, AllocProc, FreeProc);

  auto value = CBAvailable(sut);

  BOOST_TEST(value == 0);
}

BOOST_AUTO_TEST_CASE(free_space_when_empty_returns_capacity) {
  auto sut = CBCreateEx(64, AllocProc, FreeProc);

  auto value = CBFreeSpace(sut);

  BOOST_TEST(value == 64);
}

BOOST_AUTO_TEST_CASE(write_with_sufficient_space_returns_true) {
  auto sut = CBCreateEx(64, AllocProc, FreeProc);
  uint8_t buf[32] = {1};

  auto ret = CBWrite(sut, buf, sizeof(buf));

  BOOST_TEST(ret);
}

BOOST_AUTO_TEST_CASE(capacity_with_bytes_written_returns_total_size) {
  auto sut = CBCreateEx(64, AllocProc, FreeProc);
  uint8_t buf[16] = {1};
  CBWrite(sut, buf, sizeof(buf));

  auto value = CBCapacity(sut);

  BOOST_TEST(value == 64);
}

BOOST_AUTO_TEST_CASE(available_with_bytes_written_returns_bytes_written) {
  auto sut = CBCreateEx(64, AllocProc, FreeProc);
  uint8_t buf[16] = {1};
  CBWrite(sut, buf, sizeof(buf));

  auto value = CBAvailable(sut);

  BOOST_TEST(value == sizeof(buf));
}

BOOST_AUTO_TEST_CASE(free_space_when_non_empty_returns_unwritten_bytes) {
  auto sut = CBCreateEx(64, AllocProc, FreeProc);
  uint8_t buf[30] = {1};
  CBWrite(sut, buf, sizeof(buf));

  auto value = CBFreeSpace(sut);

  BOOST_TEST(value == 34);
}

BOOST_AUTO_TEST_CASE(clearing_buffer_resets_available) {
  auto sut = CBCreateEx(64, AllocProc, FreeProc);
  uint8_t buf[32] = {1};
  CBWrite(sut, buf, sizeof(buf));
  CBClear(sut);

  auto value = CBAvailable(sut);

  BOOST_TEST(value == 0);
}

BOOST_AUTO_TEST_CASE(clearing_buffer_resets_free_space) {
  auto sut = CBCreateEx(64, AllocProc, FreeProc);
  uint8_t buf[32] = {1};
  CBWrite(sut, buf, sizeof(buf));
  CBClear(sut);

  auto value = CBFreeSpace(sut);

  BOOST_TEST(value == 64);
}

BOOST_AUTO_TEST_CASE(discarding_from_buffer_updates_available) {
  auto sut = CBCreateEx(64, AllocProc, FreeProc);
  uint8_t buf[32] = {1};
  CBWrite(sut, buf, sizeof(buf));
  CBDiscard(sut, 10);

  auto value = CBAvailable(sut);

  BOOST_TEST(value == 22);
}

BOOST_AUTO_TEST_CASE(discarding_from_buffer_updates_free_space) {
  auto sut = CBCreateEx(64, AllocProc, FreeProc);
  uint8_t buf[32] = {1};
  CBWrite(sut, buf, sizeof(buf));
  CBDiscard(sut, 10);

  auto value = CBFreeSpace(sut);

  BOOST_TEST(value == 42);
}

BOOST_AUTO_TEST_CASE(discarding_some_of_available_returns_bytes_discarded) {
  auto sut = CBCreateEx(64, AllocProc, FreeProc);
  uint8_t buf[32] = {1};
  CBWrite(sut, buf, sizeof(buf));

  auto value = CBDiscard(sut, 10);

  BOOST_TEST(value == 10);
}

BOOST_AUTO_TEST_CASE(discarding_all_available_returns_bytes_discarded) {
  auto sut = CBCreateEx(64, AllocProc, FreeProc);
  uint8_t buf[32] = {1};
  CBWrite(sut, buf, sizeof(buf));

  auto value = CBDiscard(sut, 32);

  BOOST_TEST(value == 32);
}

BOOST_AUTO_TEST_CASE(discarding_more_than_available_returns_bytes_discarded) {
  auto sut = CBCreateEx(64, AllocProc, FreeProc);
  uint8_t buf[32] = {1};
  CBWrite(sut, buf, sizeof(buf));

  auto value = CBDiscard(sut, 64);

  BOOST_TEST(value == 32);
}

BOOST_AUTO_TEST_CASE(write_less_than_free_returns_true) {
  auto sut = CBCreateEx(64, AllocProc, FreeProc);
  uint8_t buf[32] = {1};

  auto value = CBWrite(sut, buf, sizeof(buf));

  BOOST_TEST(value == true);
}

BOOST_AUTO_TEST_CASE(write_equal_free_returns_true) {
  auto sut = CBCreateEx(32, AllocProc, FreeProc);
  uint8_t buf[32] = {1};

  auto value = CBWrite(sut, buf, sizeof(buf));

  BOOST_TEST(value == true);
}

BOOST_AUTO_TEST_CASE(write_more_than_free_returns_false) {
  auto sut = CBCreateEx(31, AllocProc, FreeProc);
  uint8_t buf[32] = {1};

  auto value = CBWrite(sut, buf, sizeof(buf));

  BOOST_TEST(value == false);
}

BOOST_AUTO_TEST_CASE(write_available_less_than_available_writes_all_bytes) {
  auto sut = CBCreateEx(64, AllocProc, FreeProc);
  uint8_t buf[32];
  PopulateBuffer(buf, sizeof(buf));
  uint8_t read_buf[32];

  CBWriteAvailable(sut, buf, sizeof(buf));

  BOOST_TEST(CBRead(sut, read_buf, sizeof(read_buf)));
  BOOST_TEST(memcmp(buf, read_buf, sizeof(buf)) == 0);
}

BOOST_AUTO_TEST_CASE(write_available_more_than_available_writes_bytes) {
  static const size_t kBufferSize = 31;
  auto sut = CBCreateEx(kBufferSize, AllocProc, FreeProc);
  uint8_t buf[32];
  PopulateBuffer(buf, sizeof(buf));
  uint8_t read_buf[32];

  CBWriteAvailable(sut, buf, sizeof(buf));

  BOOST_TEST(CBRead(sut, read_buf, kBufferSize));
  BOOST_TEST(memcmp(buf, read_buf, kBufferSize) == 0);
}

BOOST_AUTO_TEST_CASE(write_available_less_than_available_returns_write_count) {
  auto sut = CBCreateEx(64, AllocProc, FreeProc);
  uint8_t buf[32] = {1};

  auto value = CBWriteAvailable(sut, buf, sizeof(buf));

  BOOST_TEST(value == 32);
}

BOOST_AUTO_TEST_CASE(write_available_more_than_available_returns_write_count) {
  auto sut = CBCreateEx(42, AllocProc, FreeProc);
  uint8_t buf[32] = {1};
  CBWrite(sut, buf, sizeof(buf));

  auto value = CBWriteAvailable(sut, buf, sizeof(buf));

  BOOST_TEST(value == 10);
}

BOOST_AUTO_TEST_CASE(read_available_less_than_available_reads_all_bytes) {
  auto sut = CBCreateEx(64, AllocProc, FreeProc);
  uint8_t buf[32];
  uint8_t read_buf[32];
  PopulateBuffer(buf, sizeof(buf));
  CBWrite(sut, buf, sizeof(buf));

  CBReadAvailable(sut, read_buf, sizeof(read_buf));

  BOOST_TEST(memcmp(buf, read_buf, sizeof(read_buf)) == 0);
}

BOOST_AUTO_TEST_CASE(read_available_less_than_available_returns_read_count) {
  auto sut = CBCreateEx(64, AllocProc, FreeProc);
  uint8_t buf[32] = {1};
  uint8_t read_buf[32];
  CBWrite(sut, buf, sizeof(buf));

  auto value = CBReadAvailable(sut, read_buf, sizeof(read_buf));

  BOOST_TEST(value == 32);
}

BOOST_AUTO_TEST_CASE(read_available_more_than_available_reads_all_bytes) {
  auto sut = CBCreateEx(64, AllocProc, FreeProc);
  uint8_t buf[16];
  uint8_t read_buf[32];
  PopulateBuffer(buf, sizeof(buf));
  CBWrite(sut, buf, sizeof(buf));

  CBReadAvailable(sut, read_buf, sizeof(read_buf));

  BOOST_TEST(memcmp(buf, read_buf, sizeof(buf)) == 0);
}

BOOST_AUTO_TEST_CASE(read_available_more_than_available_returns_read_count) {
  auto sut = CBCreateEx(64, AllocProc, FreeProc);
  uint8_t buf[16] = {1};
  uint8_t read_buf[32];
  CBWrite(sut, buf, sizeof(buf));

  auto value = CBReadAvailable(sut, read_buf, sizeof(read_buf));

  BOOST_TEST(value == 16);
}

BOOST_AUTO_TEST_CASE(read_all_available_reads_all_bytes) {
  auto sut = CBCreateEx(64, AllocProc, FreeProc);
  uint8_t buf[32];
  uint8_t read_buf[32];
  PopulateBuffer(buf, sizeof(buf));
  CBWrite(sut, buf, sizeof(buf));

  CBRead(sut, read_buf, sizeof(read_buf));

  BOOST_TEST(memcmp(buf, read_buf, sizeof(read_buf)) == 0);
}

BOOST_AUTO_TEST_CASE(read_all_available_returns_true) {
  auto sut = CBCreateEx(64, AllocProc, FreeProc);
  uint8_t buf[32];
  uint8_t read_buf[32];
  PopulateBuffer(buf, sizeof(buf));
  CBWrite(sut, buf, sizeof(buf));

  auto value = CBRead(sut, read_buf, sizeof(read_buf));

  BOOST_TEST(value == true);
}

BOOST_AUTO_TEST_CASE(read_more_than_available_returns_false) {
  auto sut = CBCreateEx(64, AllocProc, FreeProc);
  uint8_t buf[31];
  uint8_t read_buf[32];
  PopulateBuffer(buf, sizeof(buf));
  CBWrite(sut, buf, sizeof(buf));

  auto value = CBRead(sut, read_buf, sizeof(read_buf));

  BOOST_TEST(value == false);
}

BOOST_AUTO_TEST_CASE(after_rolling_cursor_write_and_read_work) {
  auto sut = CBCreateEx(30, AllocProc, FreeProc);
  uint8_t buf[20];
  PopulateBuffer(buf, sizeof(buf));
  uint8_t read_buf[20];
  CBWrite(sut, buf, sizeof(buf));
  CBDiscard(sut, sizeof(buf));

  auto value = CBWrite(sut, buf, sizeof(buf));

  BOOST_TEST(value == true);
  BOOST_TEST(CBRead(sut, read_buf, sizeof(read_buf)));
  BOOST_TEST(memcmp(buf, read_buf, sizeof(read_buf)) == 0);
}

BOOST_AUTO_TEST_CASE(after_rolling_cursor_free_space_works) {
  auto sut = CBCreateEx(30, AllocProc, FreeProc);
  uint8_t buf[20];
  CBWrite(sut, buf, sizeof(buf));
  CBDiscard(sut, sizeof(buf));
  CBWrite(sut, buf, sizeof(buf));

  auto value = CBFreeSpace(sut);

  BOOST_TEST(value == 10);
}

BOOST_AUTO_TEST_CASE(after_rolling_cursor_buffer_may_be_filled_to_capacity) {
  auto sut = CBCreateEx(30, AllocProc, FreeProc);
  uint8_t buf[20];
  PopulateBuffer(buf, sizeof(buf));
  uint8_t read_buf[20];
  CBWrite(sut, buf, sizeof(buf));
  CBDiscard(sut, sizeof(buf));
  CBWrite(sut, buf, sizeof(buf));

  auto value = CBWrite(sut, buf, 10);

  BOOST_TEST(value == true);
  BOOST_TEST(CBRead(sut, read_buf, sizeof(read_buf)));
  BOOST_TEST(memcmp(buf, read_buf, sizeof(read_buf)) == 0);
}

BOOST_AUTO_TEST_SUITE_END()
