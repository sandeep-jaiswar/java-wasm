#pragma once

// Native text completion on top of llama.cpp (MIT). Optional: built only with WB_LLM=ON.
// The core does not depend on this module. Model weights are not bundled; pass a GGUF path.

#include <memory>
#include <string>

namespace wb::llm {

struct Options {
    int n_ctx = 2048;          // prompt + generated tokens must fit in this window
    int n_threads = 4;         // CPU threads for generation
    int max_new_tokens = 256;  // hard cap on generated tokens per call
};

class Model {
public:
    // Returns nullptr and sets *error when the file cannot be loaded.
    static std::unique_ptr<Model> load(const std::string& gguf_path, const Options& opt,
                                       std::string* error);

    ~Model();
    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;

    // Greedy completion of raw text (no chat template). Deterministic for a given model.
    // Returns false and sets *error on failure, for example when the prompt does not fit in n_ctx.
    bool complete(const std::string& prompt, std::string* out, std::string* error);

    // Greedy completion in the ChatML format used by Qwen2.5-Coder instruct models.
    bool chat(const std::string& system, const std::string& user, std::string* out,
              std::string* error);

private:
    struct Impl;
    explicit Model(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace wb::llm
