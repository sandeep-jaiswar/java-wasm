#include <cstdio>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "graph.hpp"
#include "trace.hpp"

namespace {

int failures = 0;
int passed = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (cond) {                                                              \
            ++passed;                                                            \
        } else {                                                                 \
            ++failures;                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

using namespace wb::graph;

// A node that bumps counter "n" by one. Used for cycles and checkpoints.
Patch bump(const State& s, Trace&) {
    const nlohmann::json* n = s.get("n");
    return {{"n", (n ? n->get<int>() : 0) + 1}};
}

Options opts(std::size_t max_steps, std::size_t trace_capacity = 256, std::size_t checkpoint_depth = 8) {
    Options o;
    o.max_steps = max_steps;
    o.trace_capacity = trace_capacity;
    o.checkpoint_depth = checkpoint_depth;
    return o;
}

bool has_kind(const Trace& t, EventKind kind) {
    for (const Event& e : t.events()) {
        if (e.kind == kind) return true;
    }
    return false;
}

std::string error_message(const Trace& t) {
    for (const Event& e : t.events()) {
        if (e.kind == EventKind::error) return e.message;
    }
    return "";
}

void test_linear() {
    Graph g;
    g.add_node("a", [](const State&, Trace& tr) {
         tr.log("in a");
         return Patch{{"a", 1}};
     })
        .add_node("b", [](const State& s, Trace&) {
            return Patch{{"b", s.get("a")->get<int>() + 1}};
        })
        .add_node("c", [](const State& s, Trace&) {
            return Patch{{"c", s.get("b")->get<int>() + 1}, {"done", true}};
        })
        .set_start("a")
        .add_edge("a", "b")
        .add_edge("b", "c")
        .add_edge("c", kEnd);

    const Result r = g.run(State{});
    CHECK(r.status == Status::ok);
    CHECK(r.steps_run == 3);
    CHECK(r.state.get("a") && r.state.get("a")->get<int>() == 1);
    CHECK(r.state.get("b") && r.state.get("b")->get<int>() == 2);
    CHECK(r.state.get("c") && r.state.get("c")->get<int>() == 3);
    CHECK(r.state.get("done") && r.state.get("done")->get<bool>());
    CHECK(r.error.empty());

    // Step events come in node order, with a duration on each step_end.
    std::string order;
    for (const Event& e : r.trace.events()) {
        if (e.kind == EventKind::step_start) order += e.node;
        if (e.kind == EventKind::step_end) CHECK(e.duration_ns >= 0);
    }
    CHECK(order == "abc");
    CHECK(has_kind(r.trace, EventKind::log));
}

void test_conditional() {
    auto build = [](int seed_value) {
        Graph g;
        g.add_node("classify", [seed_value](const State&, Trace&) {
             return Patch{{"n", seed_value}};
         })
            .add_node("even", [](const State&, Trace&) { return Patch{{"branch", "even"}}; })
            .add_node("odd", [](const State&, Trace&) { return Patch{{"branch", "odd"}}; })
            .set_start("classify")
            .add_conditional_edge("classify", [](const State& s) {
                return s.get("n")->get<int>() % 2 == 0 ? std::string("even") : std::string("odd");
            })
            .add_edge("even", kEnd)
            .add_edge("odd", kEnd);
        return g;
    };

    const Result odd = build(3).run(State{});
    CHECK(odd.status == Status::ok);
    CHECK(odd.state.get("branch") && odd.state.get("branch")->get<std::string>() == "odd");
    CHECK(odd.steps_run == 2);

    const Result even = build(4).run(State{});
    CHECK(even.status == Status::ok);
    CHECK(even.state.get("branch") && even.state.get("branch")->get<std::string>() == "even");
    CHECK(even.steps_run == 2);
}

void test_throwing_node() {
    Graph g;
    g.add_node("ok", [](const State&, Trace&) { return Patch{{"first", 1}}; })
        .add_node("boom", [](const State&, Trace&) -> Patch {
            throw std::runtime_error("kaboom");
        })
        .add_node("never", [](const State&, Trace&) { return Patch{{"never", 1}}; })
        .set_start("ok")
        .add_edge("ok", "boom")
        .add_edge("boom", "never")
        .add_edge("never", kEnd);

    const Result r = g.run(State{});
    CHECK(r.status == Status::failed);
    CHECK(r.error == "kaboom");
    CHECK(error_message(r.trace) == "kaboom");
    CHECK(r.steps_run == 1);
    CHECK(r.state.get("first") != nullptr);
    CHECK(r.state.get("never") == nullptr);
    // The failing step's pre-state is the last checkpoint.
    CHECK(!r.checkpoints.empty() && r.checkpoints.back().node == "boom");

    // A non-std exception is also caught.
    Graph g2;
    g2.add_node("x", [](const State&, Trace&) -> Patch { throw 42; })
        .set_start("x")
        .add_edge("x", kEnd);
    const Result r2 = g2.run(State{});
    CHECK(r2.status == Status::failed);
    CHECK(has_kind(r2.trace, EventKind::error));

    // A throwing router is also a failed run.
    Graph g3;
    g3.add_node("x", [](const State&, Trace&) { return Patch{}; })
        .set_start("x")
        .add_conditional_edge("x", [](const State&) -> std::string { throw std::runtime_error("route"); });
    const Result r3 = g3.run(State{});
    CHECK(r3.status == Status::failed);
    CHECK(r3.error.find("route") != std::string::npos);
}

void test_bad_edges_fail_cleanly() {
    Graph g;
    g.add_node("a", [](const State&, Trace&) { return Patch{}; })
        .set_start("a")
        .add_edge("a", "missing");
    const Result r = g.run(State{});
    CHECK(r.status == Status::failed);
    CHECK(r.error.find("missing") != std::string::npos);

    Graph no_edge;
    no_edge.add_node("a", [](const State&, Trace&) { return Patch{}; }).set_start("a");
    CHECK(no_edge.run(State{}).status == Status::failed);

    Graph bad_builder;
    bad_builder.add_node("a", [](const State&, Trace&) { return Patch{}; });
    bool threw = false;
    try {
        bad_builder.add_node("END", [](const State&, Trace&) { return Patch{}; });
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

void test_cycle_hits_max_steps() {
    Graph g;
    g.add_node("loop", bump).set_start("loop").add_edge("loop", "loop");

    const Result r = g.run(State{}, opts(10));
    CHECK(r.status == Status::max_steps);
    CHECK(r.steps_run == 10);
    CHECK(r.state.get("n") && r.state.get("n")->get<int>() == 10);
    CHECK(has_kind(r.trace, EventKind::error));

    // Reaching END on exactly the last allowed step is still ok.
    Graph exact;
    exact.add_node("a", bump).add_node("b", bump).set_start("a").add_edge("a", "b").add_edge("b", kEnd);
    const Result e = exact.run(State{}, opts(2));
    CHECK(e.status == Status::ok);
    CHECK(e.steps_run == 2);
}

void test_trace_capacity_direct() {
    Trace t(256);
    for (int i = 0; i < 10000; ++i) t.log("event " + std::to_string(i));
    CHECK(t.size() == 256);
    CHECK(t.dropped() == 9744);
    const auto evs = t.events();
    CHECK(evs.size() == 256);
    bool contiguous = true;
    for (std::size_t i = 1; i < evs.size(); ++i) {
        if (evs[i].seq != evs[i - 1].seq + 1) contiguous = false;
    }
    CHECK(contiguous);
    CHECK(evs.front().seq == 9744);
    CHECK(evs.back().seq == 9999);
    CHECK(evs.back().message == "event 9999");
}

void test_trace_capacity_long_run() {
    Graph g;
    g.add_node("loop", bump).set_start("loop").add_edge("loop", "loop");

    const Result r = g.run(State{}, opts(10000, 256));
    CHECK(r.status == Status::max_steps);
    CHECK(r.steps_run == 10000);
    // Each step emits step_start, step_end and edge, plus one final error event.
    const std::uint64_t total = r.trace.total_recorded();
    CHECK(total == 10000 * 3 + 1);
    CHECK(r.trace.size() == 256);
    CHECK(r.trace.dropped() == total - 256);
    const auto evs = r.trace.events();
    CHECK(evs.size() == 256);
    bool contiguous = true;
    for (std::size_t i = 1; i < evs.size(); ++i) {
        if (evs[i].seq != evs[i - 1].seq + 1) contiguous = false;
    }
    CHECK(contiguous);
    CHECK(evs.back().kind == EventKind::error);
}

void test_zero_capacity_is_clamped() {
    Trace t(0);
    CHECK(t.capacity() == 1);
    t.log("a");
    t.log("b");
    CHECK(t.size() == 1);
    CHECK(t.dropped() == 1);
    CHECK(t.events().front().message == "b");
}

void test_message_truncation_utf8() {
    // 300 three-byte characters (900 bytes) must be cut to <= 512 bytes on a boundary.
    std::string long_msg;
    for (int i = 0; i < 300; ++i) long_msg += "\xE2\x82\xAC";  // euro sign
    // A lone invalid byte mixed in must be replaced, not passed through.
    std::string bad = "ok\xFF" "end";

    Trace t(8);
    t.log(long_msg);
    t.log(bad);
    const auto evs = t.events();
    CHECK(evs.size() == 2);
    CHECK(evs[0].message.size() <= kMaxMessageBytes);
    CHECK(evs[0].message.size() % 3 == 0);
    CHECK(evs[1].message == "ok\xEF\xBF\xBD" "end");

    // to_json must dump without throwing and parse back.
    const std::string dumped = t.to_json().dump();
    const auto parsed = nlohmann::json::parse(dumped);
    CHECK(parsed.is_array() && parsed.size() == 2);
    CHECK(parsed[0]["message"].get<std::string>().size() <= kMaxMessageBytes);
}

void test_checkpoint_ring() {
    Graph g;
    g.add_node("loop", bump).set_start("loop").add_edge("loop", "loop");

    const Result r = g.run(State{}, opts(20, 256, 8));
    CHECK(r.status == Status::max_steps);
    CHECK(r.checkpoints.size() == 8);
    // Last 8 steps are 12..19. Before step k the counter n is k.
    CHECK(r.checkpoints.front().step_index == 12);
    CHECK(r.checkpoints.back().step_index == 19);
    CHECK(r.checkpoints.front().state.get("n") && r.checkpoints.front().state.get("n")->get<int>() == 12);
    CHECK(r.checkpoints.back().state.get("n") && r.checkpoints.back().state.get("n")->get<int>() == 19);
    CHECK(r.checkpoints.back().node == "loop");

    const Result none = g.run(State{}, opts(5, 256, 0));
    CHECK(none.checkpoints.empty());
}

void test_trace_json_shape() {
    Graph g;
    g.add_node("a", [](const State&, Trace& tr) {
         tr.log("hello");
         return Patch{{"x", 1}};
     })
        .set_start("a")
        .add_edge("a", kEnd);
    const Result r = g.run(State{});
    const auto parsed = nlohmann::json::parse(r.trace.to_json().dump());
    CHECK(parsed.is_array());
    CHECK(parsed.size() == r.trace.size());
    const char* keys[] = {"seq", "node", "kind", "message", "timestamp_ns", "duration_ns", "step_index"};
    bool all_keys = true;
    for (const auto& ev : parsed) {
        for (const char* k : keys) {
            if (!ev.contains(k)) all_keys = false;
        }
    }
    CHECK(all_keys);
    CHECK(parsed[0]["kind"] == "step_start");
    CHECK(parsed[0]["node"] == "a");
}

}  // namespace

int main() {
    test_linear();
    test_conditional();
    test_throwing_node();
    test_bad_edges_fail_cleanly();
    test_cycle_hits_max_steps();
    test_trace_capacity_direct();
    test_trace_capacity_long_run();
    test_zero_capacity_is_clamped();
    test_message_truncation_utf8();
    test_checkpoint_ring();
    test_trace_json_shape();

    std::printf("test_graph: %d passed, %d failed\n", passed, failures);
    return failures == 0 ? 0 : 1;
}
