#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace wb::graph {

// Messages are cut at this many bytes, on a UTF-8 code point boundary.
inline constexpr std::size_t kMaxMessageBytes = 512;

enum class EventKind { step_start, step_end, log, error, edge };

const char* to_string(EventKind kind);

struct Event {
    std::uint64_t seq = 0;          // monotonic, starts at 0, never reused
    std::string node;               // node the event belongs to
    EventKind kind = EventKind::log;
    std::string message;            // <= kMaxMessageBytes, always valid UTF-8
    std::int64_t timestamp_ns = 0;  // steady clock
    std::int64_t duration_ns = 0;   // set on step_end, 0 otherwise
    std::uint64_t step_index = 0;   // index of the step this event belongs to
};

// Fixed-capacity ring buffer of events. Memory is O(capacity) regardless of how
// many events are recorded; when full, the oldest event is dropped and counted.
class Trace {
public:
    // A capacity of 0 is clamped to 1.
    explicit Trace(std::size_t capacity = 256);

    // Sets the node and step index that subsequent records are attributed to.
    // The graph runner calls this before each node; nodes just call log().
    void set_context(std::string node, std::uint64_t step_index);

    // Records a log line attributed to the current context.
    void log(std::string message);

    // Records an event attributed to the current context.
    void record(EventKind kind, std::string message, std::int64_t duration_ns = 0);

    std::size_t capacity() const { return buf_.size(); }
    std::size_t size() const { return count_; }
    std::uint64_t dropped() const { return dropped_; }
    // Total events ever recorded (also the seq the next event will get).
    std::uint64_t total_recorded() const { return next_seq_; }

    // Retained events, oldest first.
    std::vector<Event> events() const;

    // JSON array of the retained events, oldest first. Keys: seq, node, kind,
    // message, timestamp_ns, duration_ns, step_index.
    nlohmann::json to_json() const;

private:
    std::vector<Event> buf_;
    std::size_t head_ = 0;   // next write slot
    std::size_t count_ = 0;  // retained events
    std::uint64_t dropped_ = 0;
    std::uint64_t next_seq_ = 0;
    std::string node_;
    std::uint64_t step_ = 0;
};

// Steady-clock time in nanoseconds since an arbitrary epoch.
std::int64_t monotonic_ns();

// Copies at most max_bytes of s, cut on a code point boundary, replacing invalid
// UTF-8 bytes with U+FFFD. The result is always valid UTF-8.
std::string sanitize_utf8(const std::string& s, std::size_t max_bytes);

}  // namespace wb::graph
