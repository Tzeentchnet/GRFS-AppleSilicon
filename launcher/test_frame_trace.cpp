#include "FrameTrace.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

grfs::FrameTraceConfig TestConfig(size_t capacity = 64,
                                  uint64_t duration_ns = 1000000) {
  grfs::FrameTraceConfig config;
  config.enabled = true;
  config.capacity = capacity;
  config.duration_ns = duration_ns;
  config.redaction_salt = 0x123456789ABCDEF0ull;
  return config;
}

void TestDisabledTrace() {
  grfs::FrameTrace trace({});
  assert(trace.BeginGuestSwap(1) == 0);
  assert(!trace.RecordAt(grfs::FrameTraceEventType::kGpuSubmit, 2, 1, 1, 0, 0,
                         grfs::FrameTraceState::kSuccess,
                         grfs::kFrameTraceHasCorrelation));
  assert(trace.Snapshot().empty());
}

void TestGuestIdsAndFifo() {
  grfs::FrameTrace trace(TestConfig());
  const uint64_t first = trace.BeginGuestSwap(100);
  const uint64_t second = trace.BeginGuestSwap(110);
  assert(first == 1 && second == 2);
  trace.EndGuestSwap(first, 120);
  trace.EndGuestSwap(second, 140);
  assert(trace.AcquireGuestFrame() == first);
  assert(trace.AcquireGuestFrame() == second);
  assert(trace.AcquireGuestFrame() == 0);

  const auto events = trace.Snapshot();
  assert(events.size() == 4);
  for (size_t index = 0; index < events.size(); ++index) {
    assert(events[index].sequence == index + 1);
    if (index) {
      assert(events[index].timestamp_ns >= events[index - 1].timestamp_ns);
    }
  }
  assert(events[2].duration_ns == 20);
  assert(events[2].fields & grfs::kFrameTraceHasDuration);
}

void TestMissingAndDuplicateSamples() {
  grfs::FrameTrace trace(TestConfig());
  trace.EndGuestSwap(41, 100);
  assert(trace.RecordAt(grfs::FrameTraceEventType::kGpuComplete, 110, 41, 9, 0, 0,
                        grfs::FrameTraceState::kSuccess,
                        grfs::kFrameTraceHasCorrelation));
  assert(trace.RecordAt(grfs::FrameTraceEventType::kGpuComplete, 120, 41, 10, 0, 0,
                        grfs::FrameTraceState::kSuccess,
                        grfs::kFrameTraceHasCorrelation));
  const auto events = trace.Snapshot();
  assert(events[0].state == grfs::FrameTraceState::kUnavailable);
  assert(!(events[0].fields & grfs::kFrameTraceHasDuration));
  assert(events[2].state == grfs::FrameTraceState::kDuplicate);
}

void TestOutputVersionCorrelation() {
  grfs::FrameTrace trace(TestConfig());
  assert(trace.RecordAt(grfs::FrameTraceEventType::kGuestOutputReady, 100, 7, 23, 0,
                        0, grfs::FrameTraceState::kSuccess,
                        grfs::kFrameTraceHasCorrelation));
  assert(trace.RecordAt(grfs::FrameTraceEventType::kPresentEnqueue, 110, 0, 23, 4,
                        0, grfs::FrameTraceState::kSuccess,
                        grfs::kFrameTraceHasCorrelation |
                            grfs::kFrameTraceHasDuration));
  const auto events = trace.Snapshot();
  assert(events[1].guest_frame_id == 7);
  assert(events[1].duration_ns == 4);
}

void TestBounds() {
  grfs::FrameTrace capacity_trace(TestConfig(2));
  assert(capacity_trace.RecordAt(grfs::FrameTraceEventType::kIssueSwapBegin, 1, 1,
                                 0, 0, 0, grfs::FrameTraceState::kSuccess, 0));
  assert(capacity_trace.RecordAt(grfs::FrameTraceEventType::kIssueSwapEnd, 2, 1, 0,
                                 1, 0, grfs::FrameTraceState::kSuccess,
                                 grfs::kFrameTraceHasDuration));
  assert(capacity_trace.complete());
  assert(!capacity_trace.RecordAt(grfs::FrameTraceEventType::kGpuSubmit, 3, 1, 1,
                                  0, 0, grfs::FrameTraceState::kSuccess,
                                  grfs::kFrameTraceHasCorrelation));
  assert(capacity_trace.event_count() == 2);
  assert(capacity_trace.dropped_events() == 1);

  grfs::FrameTrace duration_trace(TestConfig(10, 10));
  assert(duration_trace.RecordAt(grfs::FrameTraceEventType::kIssueSwapBegin, 100, 1,
                                 0, 0, 0, grfs::FrameTraceState::kSuccess, 0));
  assert(!duration_trace.RecordAt(grfs::FrameTraceEventType::kIssueSwapEnd, 111, 1,
                                  0, 11, 0, grfs::FrameTraceState::kSuccess,
                                  grfs::kFrameTraceHasDuration));
  assert(duration_trace.complete());
  assert(duration_trace.ignored_events() == 1);
}

void TestCsvSchemaAndRedaction() {
  grfs::FrameTrace trace(TestConfig());
  constexpr uint64_t raw_key = 0x1122334455667788ull;
  assert(trace.RecordAt(grfs::FrameTraceEventType::kPipelineLookup, 100, 0, 0, 0,
                        raw_key, grfs::FrameTraceState::kCacheMiss,
                        grfs::kFrameTraceHasKey));
  assert(trace.RecordAt(grfs::FrameTraceEventType::kPipelineCompile, 120, 0, 0, 20,
                        raw_key, grfs::FrameTraceState::kSuccess,
                        grfs::kFrameTraceHasDuration |
                            grfs::kFrameTraceHasKey));

  const std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("grfs-frame-trace-" + std::to_string(getpid()) + ".csv");
  assert(trace.WriteCsv(path.string()));
  std::ifstream input(path);
  const std::string csv((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>());
  std::filesystem::remove(path);
  assert(csv.find("# grfs_frame_trace_schema=1") != std::string::npos);
  assert(csv.find("event_sequence,timestamp_ns,guest_frame_id,event_type") !=
         std::string::npos);
  assert(csv.find("pipeline_lookup") != std::string::npos);
  assert(csv.find("0x1122334455667788") == std::string::npos);
  assert(csv.find(",,success\n") == std::string::npos);
  const auto events = trace.Snapshot();
  assert(events[0].redacted_key_hash != raw_key);
  assert(events[0].redacted_key_hash == events[1].redacted_key_hash);
}

void TestConcurrentBoundedSession() {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("grfs-frame-trace-concurrent-" + std::to_string(getpid()) + ".csv");
  {
    grfs::FrameTraceSession session(path.string(), TestConfig(100));
    std::vector<std::thread> threads;
    for (uint64_t thread = 0; thread < 4; ++thread) {
      threads.emplace_back([thread, &session] {
        for (uint64_t event = 0; event < 50; ++event) {
          session.Record(grfs::FrameTraceEventType::kPipelineLookup, 0, 0, 0,
                         (thread << 32) | event,
                         grfs::FrameTraceState::kCacheMiss,
                         grfs::kFrameTraceHasKey);
        }
      });
    }
    for (std::thread& thread : threads) {
      thread.join();
    }
    assert(session.trace().event_count() == 100);
    assert(session.trace().dropped_events() == 100);
    assert(session.Flush());
  }
  std::ifstream input(path);
  const std::string csv((std::istreambuf_iterator<char>(input)),
                        std::istreambuf_iterator<char>());
  std::filesystem::remove(path);
  assert(csv.find("# dropped_events=100") != std::string::npos);
  assert(csv.find("# completion_reason=capacity") != std::string::npos);
}

}  // namespace

int main() {
  TestDisabledTrace();
  TestGuestIdsAndFifo();
  TestMissingAndDuplicateSamples();
  TestOutputVersionCorrelation();
  TestBounds();
  TestCsvSchemaAndRedaction();
  TestConcurrentBoundedSession();
  std::puts("PASS: deterministic bounded frame trace");
  return 0;
}
