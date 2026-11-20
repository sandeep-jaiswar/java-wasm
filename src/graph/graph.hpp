#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "trace.hpp"

namespace wb::graph {

// Name of the terminal sentinel. It cannot be used as a node name.
inline constexpr const char* kEnd = "END";

// Key/value store. Each value is held behind a shared pointer, so copying a State
// (for a checkpoint) copies pointers, not JSON, and a patch only replaces the
// values it sets.
class State {
public:
    bool contains(const std::string& key) const;
    // nullptr when the key is absent.
    const nlohmann::json* get(const std::string& key) const;
    void set(std::string key, nlohmann::json value);
    std::size_t size() const { return entries_.size(); }
    nlohmann::json to_json() const;

private:
    friend class Graph;
    // Shallow top-level merge: each key in the patch replaces that key. Nothing
    // is deep-merged. A JSON null value is stored as null (the key is kept).
    void apply(std::map<std::string, nlohmann::json>&& patch);

    std::map<std::string, std::shared_ptr<const nlohmann::json>> entries_;
};

// Returned by a node: the keys it wants to set. Merged into the state after the node returns.
using Patch = std::map<std::string, nlohmann::json>;

// A node receives the state (read-only) and the trace (for log lines).
// Throwing from a node fails the run.
using NodeFn = std::function<Patch(const State&, Trace&)>;

// Conditional edge: returns the next node name, or kEnd.
using Router = std::function<std::string(const State&)>;

enum class Status { ok, failed, max_steps };

const char* to_string(Status status);

struct Options {
    std::size_t max_steps = 100;        // a run that would exceed this stops with max_steps
    std::size_t trace_capacity = 256;   // events kept in the trace ring
    std::size_t checkpoint_depth = 8;   // states kept in the checkpoint ring (0 disables)
};

// State as it was right before the node at step_index ran.
struct Checkpoint {
    std::size_t step_index = 0;
    std::string node;
    State state;
};

struct Result {
    Status status = Status::ok;
    State state;                  // final state; partial when the run stopped early
    std::size_t steps_run = 0;    // nodes that were executed
    std::string error;            // set when status is failed or max_steps
    Trace trace;
    std::deque<Checkpoint> checkpoints;  // oldest first, at most Options::checkpoint_depth
};

class Graph {
public:
    // Throws std::invalid_argument on an empty or duplicate name, or on the name "END".
    Graph& add_node(std::string name, NodeFn fn);

    // Throws std::invalid_argument if the name is not a node or was already set.
    Graph& set_start(std::string name);

    // Static edge. Throws std::invalid_argument if `from` already has an outgoing edge.
    Graph& add_edge(std::string from, std::string to);

    // Conditional edge. Throws std::invalid_argument if `from` already has an outgoing edge.
    Graph& add_conditional_edge(std::string from, Router route);

    // Runs from the start node until END, a failure, or max_steps. Never throws
    // for runtime problems: those end as Status::failed with an error event.
    Result run(State initial, const Options& options = {}) const;

private:
    struct Edge {
        std::string to;
        Router route;  // set only for conditional edges
    };

    std::map<std::string, NodeFn> nodes_;
    std::map<std::string, Edge> edges_;
    std::string start_;
};

}  // namespace wb::graph
