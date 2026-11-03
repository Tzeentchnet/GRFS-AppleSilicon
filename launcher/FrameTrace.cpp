#include "FrameTrace.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace grfs {
namespace {

constexpr uint64_t kNanosecondsPerSecond = 1000000000ull;

uint64_t GenerateRedactionSalt() {
  uint64_t salt = 0;
  arc4random_buf(&salt, sizeof(salt));
  salt ^= FrameTraceNowNs();
  return salt ? salt : 0x6A09E667F3BCC909ull;
}

uint64_t Mix64(uint64_t value) {
  value += 0x9E3779B97F4A7C15ull;
  value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
  value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
  return value ^ (value >> 31);
}

uint64_t ParseBoundedUnsigned(const char* name, uint64_t default_value, uint64_t minimum,
                              uint64_t maximum) {
  const char* value = std::getenv(name);
  if (!value || !*value) {
    return default_value;
  }
  errno = 0;
  char* end = nullptr;
  unsigned long long parsed = std::strtoull(value, &end, 10);
  if (errno || end == value || *end || parsed < minimum || parsed > maximum) {
    std::fprintf(stderr, "GRFS frame trace: invalid %s; using %llu\n", name,
                 static_cast<unsigned long long>(default_value));
    return default_value;
  }
  return uint64_t(parsed);
}

std::string OptionalDecimal(uint64_t value, bool available) {
  return available ? std::to_string(value) : std::string();
}

std::string OptionalHex(uint64_t value, bool available) {
  if (!available) {
    return {};
  }
  char buffer[19];
  std::snprintf(buffer, sizeof(buffer), "0x%016llx",
                static_cast<unsigned long long>(value));
  return buffer;
}

}  // namespace

uint64_t FrameTraceNowNs() {
  return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now().time_since_epoch())
                      .count());
}

const char* FrameTraceEventName(FrameTraceEventType type) {
  switch (type) {
    case FrameTraceEventType::kGuestSwapBegin:
      return "guest_swap_begin";
    case FrameTraceEventType::kGuestSwapEnd:
      return "guest_swap_end";
    case FrameTraceEventType::kGuestSwapLegacy:
      return "guest_swap_legacy";
    case FrameTraceEventType::kIssueSwapBegin:
      return "issue_swap_begin";
    case FrameTraceEventType::kIssueSwapEnd:
      return "issue_swap_end";
    case FrameTraceEventType::kGpuSubmit:
      return "gpu_submit";
    case FrameTraceEventType::kGpuComplete:
      return "gpu_complete";
    case FrameTraceEventType::kGuestOutputReady:
      return "guest_output_ready";
    case FrameTraceEventType::kPresentEnqueue:
      return "present_enqueue";
    case FrameTraceEventType::kPipelineLookup:
      return "pipeline_lookup";
    case FrameTraceEventType::kPipelineCompile:
      return "pipeline_compile";
  }
  return "unknown";
}

const char* FrameTraceStateName(FrameTraceState state) {
  switch (state) {
    case FrameTraceState::kUnavailable:
      return "unavailable";
    case FrameTraceState::kSuccess:
      return "success";
    case FrameTraceState::kFailure:
      return "failure";
    case FrameTraceState::kSkipped:
      return "skipped";
    case FrameTraceState::kCacheHit:
      return "cache_hit";
    case FrameTraceState::kCacheMiss:
      return "cache_miss";
    case FrameTraceState::kDuplicate:
      return "duplicate";
    case FrameTraceState::kLegacy:
      return "legacy";
    case FrameTraceState::kSuboptimal:
      return "suboptimal";
  }
  return "unavailable";
}

bool IsValidFrameTraceEventType(uint32_t type) {
  switch (static_cast<FrameTraceEventType>(type)) {
    case FrameTraceEventType::kGuestSwapBegin:
    case FrameTraceEventType::kGuestSwapEnd:
    case FrameTraceEventType::kGuestSwapLegacy:
    case FrameTraceEventType::kIssueSwapBegin:
    case FrameTraceEventType::kIssueSwapEnd:
    case FrameTraceEventType::kGpuSubmit:
    case FrameTraceEventType::kGpuComplete:
    case FrameTraceEventType::kGuestOutputReady:
    case FrameTraceEventType::kPresentEnqueue:
    case FrameTraceEventType::kPipelineLookup:
    case FrameTraceEventType::kPipelineCompile:
      return true;
  }
  return false;
}

bool IsValidFrameTraceState(uint32_t state) {
  return state <= static_cast<uint32_t>(FrameTraceState::kSuboptimal);
}

FrameTrace::FrameTrace(FrameTraceConfig config) : config_(config) {
  if (!config_.capacity) {
    config_.enabled = false;
  }
  if (!config_.redaction_salt) {
    config_.redaction_salt = GenerateRedactionSalt();
  }
  if (config_.enabled) {
    events_.reserve(config_.capacity);
  }
}

uint64_t FrameTrace::BeginGuestSwap(uint64_t timestamp_ns) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (!config_.enabled) {
    return 0;
  }
  if (complete_) {
    CountRejectedLocked();
    return 0;
  }
  const uint64_t frame_id = next_guest_frame_id_++;
  FrameTraceEvent event;
  event.timestamp_ns = timestamp_ns;
  event.guest_frame_id = frame_id;
  event.type = FrameTraceEventType::kGuestSwapBegin;
  event.state = FrameTraceState::kSuccess;
  if (!AppendLocked(event)) {
    return 0;
  }
  guest_swap_starts_[frame_id] = timestamp_ns;
  pending_guest_frames_.push_back(frame_id);
  return frame_id;
}

void FrameTrace::EndGuestSwap(uint64_t guest_frame_id, uint64_t timestamp_ns) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (!config_.enabled || !guest_frame_id) {
    return;
  }
  FrameTraceEvent event;
  event.timestamp_ns = timestamp_ns;
  event.guest_frame_id = guest_frame_id;
  event.type = FrameTraceEventType::kGuestSwapEnd;
  auto start = guest_swap_starts_.find(guest_frame_id);
  if (start != guest_swap_starts_.end() && timestamp_ns >= start->second) {
    event.duration_ns = timestamp_ns - start->second;
    event.fields |= kFrameTraceHasDuration;
    event.state = FrameTraceState::kSuccess;
    guest_swap_starts_.erase(start);
  } else {
    event.state = FrameTraceState::kUnavailable;
  }
  AppendLocked(event);
}

uint64_t FrameTrace::RecordLegacyGuestSwap(uint64_t timestamp_ns) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (!config_.enabled) {
    return 0;
  }
  if (complete_) {
    CountRejectedLocked();
    return 0;
  }
  const uint64_t frame_id = next_guest_frame_id_++;
  FrameTraceEvent event;
  event.timestamp_ns = timestamp_ns;
  event.guest_frame_id = frame_id;
  event.type = FrameTraceEventType::kGuestSwapLegacy;
  event.state = FrameTraceState::kLegacy;
  if (!AppendLocked(event)) {
    return 0;
  }
  pending_guest_frames_.push_back(frame_id);
  return frame_id;
}

uint64_t FrameTrace::AcquireGuestFrame() {
  std::lock_guard<std::mutex> guard(mutex_);
  if (!config_.enabled || pending_guest_frames_.empty()) {
    return 0;
  }
  const uint64_t frame_id = pending_guest_frames_.front();
  pending_guest_frames_.pop_front();
  return frame_id;
}

bool FrameTrace::RecordAt(FrameTraceEventType type, uint64_t timestamp_ns,
                          uint64_t guest_frame_id, uint64_t correlation_id,
                          uint64_t duration_ns, uint64_t raw_key_hash,
                          FrameTraceState state, uint32_t fields) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (!config_.enabled) {
    return false;
  }
  if (complete_) {
    CountRejectedLocked();
    return false;
  }
  if (type == FrameTraceEventType::kPresentEnqueue && !guest_frame_id &&
      (fields & kFrameTraceHasCorrelation)) {
    auto output_frame = output_versions_to_frames_.find(correlation_id);
    if (output_frame != output_versions_to_frames_.end()) {
      guest_frame_id = output_frame->second;
    }
  }
  if (type == FrameTraceEventType::kGpuComplete && guest_frame_id &&
      !completed_gpu_frames_.insert(guest_frame_id).second) {
    state = FrameTraceState::kDuplicate;
  }

  FrameTraceEvent event;
  event.timestamp_ns = timestamp_ns;
  event.guest_frame_id = guest_frame_id;
  event.type = type;
  event.correlation_id = correlation_id;
  event.duration_ns = duration_ns;
  event.redacted_key_hash =
      (fields & kFrameTraceHasKey) ? RedactKey(raw_key_hash) : 0;
  event.state = state;
  event.fields = fields;
  if (!AppendLocked(event)) {
    return false;
  }
  if (type == FrameTraceEventType::kGuestOutputReady && guest_frame_id &&
      (fields & kFrameTraceHasCorrelation)) {
    output_versions_to_frames_[correlation_id] = guest_frame_id;
  }
  return true;
}

std::vector<FrameTraceEvent> FrameTrace::Snapshot() const {
  std::lock_guard<std::mutex> guard(mutex_);
  return events_;
}

bool FrameTrace::WriteCsv(const std::string& path) const {
  std::vector<FrameTraceEvent> events;
  uint64_t dropped = 0;
  uint64_t ignored = 0;
  bool complete = false;
  bool capacity_exhausted = false;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    events = events_;
    dropped = dropped_events_;
    ignored = ignored_events_;
    complete = complete_;
    capacity_exhausted = capacity_exhausted_;
  }

  const std::string temporary =
      path + ".tmp." + std::to_string(static_cast<unsigned long long>(getpid()));
  int descriptor = open(temporary.c_str(),
                        O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW,
                        S_IRUSR | S_IWUSR);
  if (descriptor < 0) {
    std::fprintf(stderr, "GRFS frame trace: cannot open %s: %s\n", temporary.c_str(),
                 std::strerror(errno));
    return false;
  }
  if (fchmod(descriptor, S_IRUSR | S_IWUSR) != 0) {
    std::fprintf(stderr, "GRFS frame trace: cannot protect %s: %s\n",
                 temporary.c_str(), std::strerror(errno));
    close(descriptor);
    unlink(temporary.c_str());
    return false;
  }
  FILE* output = fdopen(descriptor, "w");
  if (!output) {
    std::fprintf(stderr, "GRFS frame trace: cannot write %s: %s\n", temporary.c_str(),
                 std::strerror(errno));
    close(descriptor);
    unlink(temporary.c_str());
    return false;
  }

  std::fprintf(output, "# grfs_frame_trace_schema=1\n");
  std::fprintf(output, "# dropped_events=%llu\n",
               static_cast<unsigned long long>(dropped));
  std::fprintf(output, "# ignored_events=%llu\n",
               static_cast<unsigned long long>(ignored));
  std::fprintf(output, "# capture_complete=%s\n", complete ? "true" : "false");
  std::fprintf(output, "# completion_reason=%s\n",
               complete ? (capacity_exhausted ? "capacity" : "duration") : "active");
  std::fputs("schema_version,event_sequence,timestamp_ns,guest_frame_id,event_type,"
             "correlation_id,duration_ns,redacted_key_hash,state\n",
             output);
  for (const FrameTraceEvent& event : events) {
    const std::string frame = OptionalDecimal(event.guest_frame_id, event.guest_frame_id != 0);
    const std::string correlation =
        OptionalDecimal(event.correlation_id,
                        (event.fields & kFrameTraceHasCorrelation) != 0);
    const std::string duration =
        OptionalDecimal(event.duration_ns, (event.fields & kFrameTraceHasDuration) != 0);
    const std::string key =
        OptionalHex(event.redacted_key_hash, (event.fields & kFrameTraceHasKey) != 0);
    std::fprintf(output, "1,%llu,%llu,%s,%s,%s,%s,%s,%s\n",
                 static_cast<unsigned long long>(event.sequence),
                 static_cast<unsigned long long>(event.timestamp_ns), frame.c_str(),
                 FrameTraceEventName(event.type), correlation.c_str(), duration.c_str(),
                 key.c_str(), FrameTraceStateName(event.state));
  }

  bool success = std::ferror(output) == 0;
  if (std::fflush(output) != 0) {
    success = false;
  }
  if (fsync(descriptor) != 0) {
    success = false;
  }
  if (std::fclose(output) != 0) {
    success = false;
  }
  if (!success) {
    std::fprintf(stderr, "GRFS frame trace: failed writing %s: %s\n",
                 temporary.c_str(), std::strerror(errno));
    unlink(temporary.c_str());
    return false;
  }
  if (std::rename(temporary.c_str(), path.c_str()) != 0) {
    std::fprintf(stderr, "GRFS frame trace: cannot publish %s: %s\n", path.c_str(),
                 std::strerror(errno));
    unlink(temporary.c_str());
    return false;
  }
  return true;
}

bool FrameTrace::enabled() const {
  std::lock_guard<std::mutex> guard(mutex_);
  return config_.enabled;
}

bool FrameTrace::complete() const {
  std::lock_guard<std::mutex> guard(mutex_);
  return complete_;
}

size_t FrameTrace::event_count() const {
  std::lock_guard<std::mutex> guard(mutex_);
  return events_.size();
}

uint64_t FrameTrace::dropped_events() const {
  std::lock_guard<std::mutex> guard(mutex_);
  return dropped_events_;
}

uint64_t FrameTrace::ignored_events() const {
  std::lock_guard<std::mutex> guard(mutex_);
  return ignored_events_;
}

bool FrameTrace::AppendLocked(FrameTraceEvent event) {
  if (!config_.enabled) {
    return false;
  }
  if (complete_) {
    CountRejectedLocked();
    return false;
  }
  if (!capture_started_) {
    capture_started_ = true;
    capture_start_ns_ = event.timestamp_ns;
  } else if (config_.duration_ns &&
             event.timestamp_ns > capture_start_ns_ &&
             event.timestamp_ns - capture_start_ns_ > config_.duration_ns) {
    complete_ = true;
    ++ignored_events_;
    guest_swap_starts_.clear();
    pending_guest_frames_.clear();
    return false;
  }
  if (events_.size() >= config_.capacity) {
    complete_ = true;
    capacity_exhausted_ = true;
    ++dropped_events_;
    guest_swap_starts_.clear();
    pending_guest_frames_.clear();
    return false;
  }
  event.timestamp_ns = std::max(event.timestamp_ns, last_timestamp_ns_);
  last_timestamp_ns_ = event.timestamp_ns;
  event.sequence = next_sequence_++;
  events_.push_back(event);
  if (events_.size() == config_.capacity) {
    complete_ = true;
    capacity_exhausted_ = true;
  }
  return true;
}

void FrameTrace::CountRejectedLocked() {
  if (capacity_exhausted_) {
    ++dropped_events_;
  } else {
    ++ignored_events_;
  }
}

uint64_t FrameTrace::RedactKey(uint64_t raw_key_hash) const {
  uint64_t redacted = Mix64(raw_key_hash ^ config_.redaction_salt);
  return redacted ? redacted : 1;
}

std::unique_ptr<FrameTraceSession> FrameTraceSession::CreateFromEnvironment() {
  const char* path = std::getenv("GRFS_FRAME_TRACE");
  if (!path || !*path) {
    return nullptr;
  }
  if (path[0] != '/') {
    std::fputs(
        "GRFS frame trace: GRFS_FRAME_TRACE must be an absolute output path; "
        "tracing is disabled\n",
        stderr);
    return nullptr;
  }
  const uint64_t seconds =
      ParseBoundedUnsigned("GRFS_FRAME_TRACE_SECONDS", 60, 1, 600);
  const uint64_t capacity =
      ParseBoundedUnsigned("GRFS_FRAME_TRACE_CAPACITY", 200000, 100, 2000000);
  FrameTraceConfig config;
  config.enabled = true;
  config.capacity = size_t(capacity);
  config.duration_ns = seconds * kNanosecondsPerSecond;
  std::fprintf(stderr,
               "GRFS frame trace: enabled for %llu seconds or %llu events; "
               "output is private and local\n",
               static_cast<unsigned long long>(seconds),
               static_cast<unsigned long long>(capacity));
  return std::make_unique<FrameTraceSession>(path, config);
}

FrameTraceSession::FrameTraceSession(std::string path, FrameTraceConfig config)
    : path_(std::move(path)), trace_(config) {}

FrameTraceSession::~FrameTraceSession() {
  Flush();
}

uint64_t FrameTraceSession::BeginGuestSwap() {
  uint64_t frame_id = trace_.BeginGuestSwap(FrameTraceNowNs());
  FlushIfComplete();
  return frame_id;
}

void FrameTraceSession::EndGuestSwap(uint64_t guest_frame_id) {
  trace_.EndGuestSwap(guest_frame_id, FrameTraceNowNs());
  FlushIfComplete();
}

uint64_t FrameTraceSession::RecordLegacyGuestSwap() {
  uint64_t frame_id = trace_.RecordLegacyGuestSwap(FrameTraceNowNs());
  FlushIfComplete();
  return frame_id;
}

uint64_t FrameTraceSession::AcquireGuestFrame() {
  return trace_.AcquireGuestFrame();
}

void FrameTraceSession::Record(FrameTraceEventType type, uint64_t guest_frame_id,
                               uint64_t correlation_id, uint64_t duration_ns,
                               uint64_t raw_key_hash, FrameTraceState state,
                               uint32_t fields) {
  trace_.RecordAt(type, FrameTraceNowNs(), guest_frame_id, correlation_id,
                  duration_ns, raw_key_hash, state, fields);
  FlushIfComplete();
}

bool FrameTraceSession::Flush() {
  std::lock_guard<std::mutex> guard(flush_mutex_);
  const size_t count = trace_.event_count();
  const uint64_t dropped = trace_.dropped_events();
  const uint64_t ignored = trace_.ignored_events();
  if (flushed_event_count_ == count && flushed_dropped_events_ == dropped &&
      flushed_ignored_events_ == ignored) {
    return true;
  }
  if (!trace_.WriteCsv(path_)) {
    return false;
  }
  flushed_event_count_ = count;
  flushed_dropped_events_ = dropped;
  flushed_ignored_events_ = ignored;
  std::fprintf(stderr, "GRFS frame trace: wrote %zu events\n", count);
  return true;
}

void FrameTraceSession::FlushIfComplete() {
  bool expected = false;
  if (trace_.complete() &&
      completion_flush_attempted_.compare_exchange_strong(expected, true)) {
    Flush();
  }
}

}  // namespace grfs
