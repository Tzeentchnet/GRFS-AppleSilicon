#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/stat.h>

extern "C" int GRFSTraceEnabled();
extern "C" uint64_t GRFSTraceGuestSwapBegin();
extern "C" void GRFSTraceGuestSwapEnd(uint64_t frame);
extern "C" uint64_t GRFSTraceAcquireGuestFrame();
extern "C" void GRFSTraceRecordEvent(uint32_t type, uint64_t frame,
                                     uint64_t correlation, uint64_t duration,
                                     uint64_t key, uint32_t state,
                                     uint32_t fields);
extern "C" void GRFSTraceFlush();

int main(int argc, char** argv) {
  if (argc == 1) {
    assert(!GRFSTraceEnabled());
    return 0;
  }
  assert(argc == 2);
  assert(GRFSTraceEnabled());
  const uint64_t frame = GRFSTraceGuestSwapBegin();
  assert(frame == 1);
  GRFSTraceGuestSwapEnd(frame);
  assert(GRFSTraceAcquireGuestFrame() == frame);
  assert(GRFSTraceAcquireGuestFrame() == 0);

  constexpr uint32_t has_correlation = 1u << 0;
  constexpr uint32_t has_duration = 1u << 1;
  constexpr uint32_t has_key = 1u << 2;
  GRFSTraceRecordEvent(10, frame, 0, 0, 0, 1, 0);
  GRFSTraceRecordEvent(11, frame, 0, 500, 0, 1, has_duration);
  GRFSTraceRecordEvent(12, frame, 9, 0, 0, 1, has_correlation);
  GRFSTraceRecordEvent(13, frame, 9, 0, 0, 1, has_correlation);
  GRFSTraceRecordEvent(20, 0, 0, 0, 0x1122334455667788ull, 5, has_key);
  GRFSTraceRecordEvent(21, 0, 0, 700, 0x1122334455667788ull, 1,
                       has_duration | has_key);
  GRFSTraceFlush();

  const std::filesystem::path path(argv[1]);
  std::ifstream input(path);
  const std::string csv((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>());
  assert(csv.find("guest_swap_begin") != std::string::npos);
  assert(csv.find("gpu_complete") != std::string::npos);
  assert(csv.find("pipeline_compile") != std::string::npos);
  assert(csv.find("0x1122334455667788") == std::string::npos);
  struct stat status {};
  assert(stat(path.c_str(), &status) == 0);
  assert((status.st_mode & 0777) == 0600);
  return 0;
}
