#include "graph.hpp"

#include <exception>
#include <stdexcept>
#include <utility>

namespace wb::graph {

bool State::contains(const std::string& key) const { return entries_.count(key) != 0; }

const nlohmann::json* State::get(const std::string& key) const {
    const auto it = entries_.find(key);
    return it == entries_.end() ? nullptr : it->second.get();
}

void State::set(std::string key, nlohmann::json value) {
    entries_[std::move(key)] = std::make_shared<const nlohmann::json>(std::move(value));
}

void State::apply(const std::map<std::string, nlohmann::json>& patch) {
    for (const auto& [key, value] : patch) {
        entries_[key] = std::make_shared<const nlohmann::json>(value);
    }
}

nlohmann::json State::to_json() const {
    nlohmann::json obj = nlohmann::json::object();
    for (const auto& [key, value] : entries_) obj[key] = *value;
    return obj;
}

const char* to_string(Status status) {
    switch (status) {
        case Status::ok: return "ok";
        case Status::failed: return "failed";
        case Status::max_steps: return "max_steps";
    }
    return "failed";
}

Graph& Graph::add_node(std::string name, NodeFn fn) {
    if (name.empty()) throw std::invalid_argument("node name must not be empty");
    if (name == kEnd) throw std::invalid_argument("\"END\" is reserved and cannot be a node name");
    if (nodes_.count(name)) throw std::invalid_argument("duplicate node: " + name);
    nodes_.emplace(std::move(name), std::move(fn));
    return *this;
}

Graph& Graph::set_start(std::string name) {
    if (nodes_.count(name) == 0) throw std::invalid_argument("start node is not registered: " + name);
    if (!start_.empty()) throw std::invalid_argument("start node already set");
    start_ = std::move(name);
    return *this;
}

Graph& Graph::add_edge(std::string from, std::string to) {
    if (edges_.count(from)) throw std::invalid_argument("node already has an outgoing edge: " + from);
    edges_.emplace(std::move(from), Edge{std::move(to), nullptr});
    return *this;
}

Graph& Graph::add_conditional_edge(std::string from, Router route) {
    if (edges_.count(from)) throw std::invalid_argument("node already has an outgoing edge: " + from);
    edges_.emplace(std::move(from), Edge{std::string(), std::move(route)});
    return *this;
}

namespace {

// Marks the run failed (or max_steps) and records the error event against `node`.
void stop(Result& r, Status status, const std::string& node, std::uint64_t step, std::string msg) {
    r.status = status;
    r.error = msg;
    r.trace.set_context(node, step);
    r.trace.record(EventKind::error, std::move(msg));
}

}  // namespace

Result Graph::run(State initial, const Options& options) const {
    Result r;
    r.trace = Trace(options.trace_capacity);
    r.state = std::move(initial);

    if (start_.empty()) {
        stop(r, Status::failed, "", 0, "no start node set");
        return r;
    }

    std::string current = start_;
    while (true) {
        if (r.steps_run >= options.max_steps) {
            stop(r, Status::max_steps, current, r.steps_run,
                 "max_steps reached (" + std::to_string(options.max_steps) + ")");
            return r;
        }

        const auto node = nodes_.find(current);
        if (node == nodes_.end()) {
            stop(r, Status::failed, current, r.steps_run, "unknown node: " + current);
            return r;
        }

        const std::uint64_t step = r.steps_run;
        if (options.checkpoint_depth > 0) {
            r.checkpoints.push_back(Checkpoint{static_cast<std::size_t>(step), current, r.state});
            while (r.checkpoints.size() > options.checkpoint_depth) r.checkpoints.pop_front();
        }

        r.trace.set_context(current, step);
        r.trace.record(EventKind::step_start, "");
        const std::int64_t started = monotonic_ns();
        try {
            Patch patch = node->second(r.state, r.trace);
            r.state.apply(patch);
        } catch (const std::exception& e) {
            stop(r, Status::failed, current, step, e.what());
            return r;
        } catch (...) {
            stop(r, Status::failed, current, step, "unknown exception");
            return r;
        }
        r.trace.record(EventKind::step_end, "", monotonic_ns() - started);
        ++r.steps_run;

        const auto edge = edges_.find(current);
        if (edge == edges_.end()) {
            stop(r, Status::failed, current, step, "node has no outgoing edge: " + current);
            return r;
        }

        std::string next;
        try {
            next = edge->second.route ? edge->second.route(r.state) : edge->second.to;
        } catch (const std::exception& e) {
            stop(r, Status::failed, current, step, std::string("router failed: ") + e.what());
            return r;
        } catch (...) {
            stop(r, Status::failed, current, step, "router failed: unknown exception");
            return r;
        }

        r.trace.record(EventKind::edge, "-> " + next);
        if (next == kEnd) {
            r.status = Status::ok;
            return r;
        }
        if (nodes_.count(next) == 0) {
            stop(r, Status::failed, current, step, "edge targets unknown node: " + next);
            return r;
        }
        current = std::move(next);
    }
}

}  // namespace wb::graph
