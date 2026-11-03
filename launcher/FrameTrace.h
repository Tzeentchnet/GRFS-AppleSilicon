#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace grfs {

enum class FrameTraceEventType : uint32_t {
  kGuestSwapBegin = 1,
  kGuestSwapEnd = 2,
  kGuestSwapLegacy = 3,
  kIssueSwapBegin = 10,
  kIssueSwapEnd = 11,
  kGpuSubmit = 12,
  kGpuComplete = 13,
  kGuestOutputReady = 14,
  kPresentEnqueue = 15,
  kPipelineLookup = 20,
  kPipelineCompile = 21,
};

enum class FrameTraceState : uint32_t {
  kUnavailable = 0,
  kSuccess = 1,
  kFailure = 2,
  kSkipped = 3,
  kCacheHit = 4,
  kCacheMiss = 5,
  kDuplicate = 6,
  kLegacy = 7,
  kSuboptimal = 8,
};

enum FrameTraceField : uint32_t {
  kFrameTraceHasCorrelation = 1u << 0,
  kFrameTraceHasDuration = 1u << 1,
  kFrameTraceHasKey = 1u << 2,
};

struct FrameTraceConfig {
  bool enabled = false;
  size_t capacity = 200000;
  uint64_t duration_ns = 60000000000ull;
  uint64_t redaction_salt = 0;
};

struct FrameTraceEvent {
  uint64_t sequence = 0;
  uint64_t timestamp_ns = 0;
  uint64_t guest_frame_id = 0;
  FrameTraceEventType type = FrameTraceEventType::kGuestSwapBegin;
  uint64_t correlation_id = 0;
  uint64_t duration_ns = 0;
  uint64_t redacted_key_hash = 0;
  FrameTraceState state = FrameTraceState::kUnavailable;
  uint32_t fields = 0;
};

class FrameTrace {
 public:
  explicit FrameTrace(FrameTraceConfig config);

  uint64_t BeginGuestSwap(uint64_t timestamp_ns);
  void EndGuestSwap(uint64_t guest_frame_id, uint64_t timestamp_ns);
  uint64_t RecordLegacyGuestSwap(uint64_t timestamp_ns);
  uint64_t AcquireGuestFrame();

  bool RecordAt(FrameTraceEventType type, uint64_t timestamp_ns, uint64_t guest_frame_id,
                uint64_t correlation_id, uint64_t duration_ns, uint64_t raw_key_hash,
                FrameTraceState state, uint32_t fields);

  std::vector<FrameTraceEvent> Snapshot() const;
  bool WriteCsv(const std::string& path) const;

  bool enabled() const;
  bool complete() const;
  size_t event_count() const;
  uint64_t dropped_events() const;
  uint64_t ignored_events() const;

 private:
  bool AppendLocked(FrameTraceEvent event);
  void CountRejectedLocked();
  uint64_t RedactKey(uint64_t raw_key_hash) const;

  FrameTraceConfig config_;
  mutable std::mutex mutex_;
  std::vector<FrameTraceEvent> events_;
  std::deque<uint64_t> pending_guest_frames_;
  std::unordered_map<uint64_t, uint64_t> guest_swap_starts_;
  std::unordered_map<uint64_t, uint64_t> output_versions_to_frames_;
  std::unordered_set<uint64_t> completed_gpu_frames_;
  uint64_t next_guest_frame_id_ = 1;
  uint64_t next_sequence_ = 1;
  uint64_t capture_start_ns_ = 0;
  uint64_t last_timestamp_ns_ = 0;
  uint64_t dropped_events_ = 0;
  uint64_t ignored_events_ = 0;
  bool capture_started_ = false;
  bool complete_ = false;
  bool capacity_exhausted_ = false;
};

class FrameTraceSession {
 public:
  static std::unique_ptr<FrameTraceSession> CreateFromEnvironment();

  FrameTraceSession(std::string path, FrameTraceConfig config);
  ~FrameTraceSession();

  uint64_t BeginGuestSwap();
  void EndGuestSwap(uint64_t guest_frame_id);
  uint64_t RecordLegacyGuestSwap();
  uint64_t AcquireGuestFrame();
  void Record(FrameTraceEventType type, uint64_t guest_frame_id, uint64_t correlation_id,
              uint64_t duration_ns, uint64_t raw_key_hash, FrameTraceState state,
              uint32_t fields);
  bool Flush();

  FrameTrace& trace() { return trace_; }
  const FrameTrace& trace() const { return trace_; }

 private:
  void FlushIfComplete();

  std::string path_;
  FrameTrace trace_;
  std::mutex flush_mutex_;
  size_t flushed_event_count_ = static_cast<size_t>(-1);
  uint64_t flushed_dropped_events_ = std::numeric_limits<uint64_t>::max();
  uint64_t flushed_ignored_events_ = std::numeric_limits<uint64_t>::max();
  std::atomic<bool> completion_flush_attempted_{false};
};

uint64_t FrameTraceNowNs();
const char* FrameTraceEventName(FrameTraceEventType type);
const char* FrameTraceStateName(FrameTraceState state);
bool IsValidFrameTraceEventType(uint32_t type);
bool IsValidFrameTraceState(uint32_t state);

}  // namespace grfs
