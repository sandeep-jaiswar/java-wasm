#include "trace.hpp"

#include <algorithm>
#include <chrono>

namespace wb::graph {

const char* to_string(EventKind kind) {
    switch (kind) {
        case EventKind::step_start: return "step_start";
        case EventKind::step_end: return "step_end";
        case EventKind::log: return "log";
        case EventKind::error: return "error";
        case EventKind::edge: return "edge";
    }
    return "log";
}

std::int64_t monotonic_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string sanitize_utf8(const std::string& s, std::size_t max_bytes) {
    static const char kReplacement[] = "\xEF\xBF\xBD";
    std::string out;
    out.reserve(std::min(s.size(), max_bytes));
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        std::size_t len = 0;
        std::uint32_t cp = 0;
        if (c < 0x80) {
            len = 1;
            cp = c;
        } else if (c >= 0xC2 && c <= 0xDF) {
            len = 2;
            cp = c & 0x1Fu;
        } else if (c >= 0xE0 && c <= 0xEF) {
            len = 3;
            cp = c & 0x0Fu;
        } else if (c >= 0xF0 && c <= 0xF4) {
            len = 4;
            cp = c & 0x07u;
        }
        bool ok = len != 0 && i + len <= s.size();
        for (std::size_t k = 1; ok && k < len; ++k) {
            const auto cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0u) != 0x80u) {
                ok = false;
            } else {
                cp = (cp << 6) | (cc & 0x3Fu);
            }
        }
        if (ok && len == 3 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF))) ok = false;
        if (ok && len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) ok = false;

        if (ok) {
            if (out.size() + len > max_bytes) break;
            out.append(s, i, len);
            i += len;
        } else {
            if (out.size() + 3 > max_bytes) break;
            out.append(kReplacement, 3);
            i += 1;
        }
    }
    return out;
}

Trace::Trace(std::size_t capacity) : buf_(capacity == 0 ? 1 : capacity) {}

void Trace::set_context(std::string node, std::uint64_t step_index) {
    node_ = sanitize_utf8(node, kMaxMessageBytes);
    step_ = step_index;
}

void Trace::log(std::string message) { record(EventKind::log, std::move(message)); }

void Trace::record(EventKind kind, std::string message, std::int64_t duration_ns) {
    if (count_ == buf_.size()) {
        ++dropped_;  // the slot at head_ holds the oldest event; overwrite it
    } else {
        ++count_;
    }
    Event& slot = buf_[head_];
    slot.seq = next_seq_++;
    slot.node = node_;
    slot.kind = kind;
    slot.message = sanitize_utf8(message, kMaxMessageBytes);
    slot.timestamp_ns = monotonic_ns();
    slot.duration_ns = duration_ns;
    slot.step_index = step_;
    head_ = (head_ + 1) % buf_.size();
}

std::vector<Event> Trace::events() const {
    std::vector<Event> out;
    out.reserve(count_);
    const std::size_t oldest = (head_ + buf_.size() - count_) % buf_.size();
    for (std::size_t i = 0; i < count_; ++i) {
        out.push_back(buf_[(oldest + i) % buf_.size()]);
    }
    return out;
}

nlohmann::json Trace::to_json() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const Event& e : events()) {
        arr.push_back({
            {"seq", e.seq},
            {"node", e.node},
            {"kind", to_string(e.kind)},
            {"message", e.message},
            {"timestamp_ns", e.timestamp_ns},
            {"duration_ns", e.duration_ns},
            {"step_index", e.step_index},
        });
    }
    return arr;
}

}  // namespace wb::graph
