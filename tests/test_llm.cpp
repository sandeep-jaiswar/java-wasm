// Native smoke test for the llama.cpp module. Usage: test_llm <model.gguf>
// With no path (or an empty one) the model checks are skipped and the test passes, so the
// suite stays green on machines without the weights.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <string>

#include "llm.hpp"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    std::string error;
    auto missing = wb::llm::Model::load("/nonexistent/model.gguf", {}, &error);
    check(missing == nullptr && !error.empty(), "load reports a missing file");

    if (argc < 2 || std::string(argv[1]).empty()) {
        std::printf("SKIP model checks (pass a .gguf path to run them)\n");
        return failures == 0 ? 0 : 1;
    }

    wb::llm::Options opt;
    opt.max_new_tokens = 32;
    auto model = wb::llm::Model::load(argv[1], opt, &error);
    check(model != nullptr, "load model");
    if (!model) {
        std::printf("error: %s\n", error.c_str());
        return 1;
    }

    std::string reply;
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = model->chat("You are a helpful assistant.",
                                "Reply with exactly one word: pong", &reply, &error);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0).count();
    check(ok, "chat completion succeeds");
    std::printf("reply (%lld ms): %s\n", static_cast<long long>(ms), reply.c_str());
    check(lower(reply).find("pong") != std::string::npos, "reply contains 'pong'");

    // Greedy decoding: the same prompt must give the same output.
    std::string again;
    check(model->chat("You are a helpful assistant.", "Reply with exactly one word: pong", &again,
                      &error) &&
              again == reply,
          "greedy output is deterministic");

    std::string too_long;
    const std::string big(20000, 'x');
    check(!model->complete(big, &too_long, &error) && error.find("context") != std::string::npos,
          "oversized prompt is rejected with a context error");

    std::printf("%s\n", failures == 0 ? "all llm checks passed" : "llm checks FAILED");
    return failures == 0 ? 0 : 1;
}
