#include "llm.hpp"

#include <vector>

// llama.cpp is pinned (see CMakeLists.txt). Its batch and decode calls are marked deprecated in
// this revision but remain supported, so the warning is silenced here only.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#include "llama.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace wb::llm {

namespace {

void set_error(std::string* error, const std::string& message) {
    if (error) *error = message;
}

void backend_once() {
    static bool done = [] {
        llama_backend_init();
        return true;
    }();
    (void)done;
}

}  // namespace

struct Model::Impl {
    llama_model* model = nullptr;
    llama_context* ctx = nullptr;
    const llama_vocab* vocab = nullptr;
    Options opt;

    ~Impl() {
        if (ctx) llama_free(ctx);
        if (model) llama_model_free(model);
    }

    // Tokenizes with special tokens parsed, so ChatML markers become single tokens.
    bool tokenize(const std::string& text, std::vector<llama_token>* out, std::string* error) {
        const int32_t needed = -llama_tokenize(vocab, text.data(), static_cast<int32_t>(text.size()),
                                               nullptr, 0, true, true);
        if (needed <= 0) {
            set_error(error, "tokenizer produced no tokens");
            return false;
        }
        out->resize(static_cast<size_t>(needed));
        const int32_t got = llama_tokenize(vocab, text.data(), static_cast<int32_t>(text.size()),
                                           out->data(), needed, true, true);
        if (got < 0) {
            set_error(error, "tokenization failed");
            return false;
        }
        out->resize(static_cast<size_t>(got));
        return true;
    }

    bool run(const std::string& prompt, std::string* out, std::string* error) {
        std::vector<llama_token> prompt_tokens;
        if (!tokenize(prompt, &prompt_tokens, error)) return false;

        const int n_ctx = llama_n_ctx(ctx);
        if (static_cast<int>(prompt_tokens.size()) + opt.max_new_tokens > n_ctx) {
            set_error(error, "prompt does not fit in the context window (" +
                                 std::to_string(prompt_tokens.size()) + " tokens, n_ctx " +
                                 std::to_string(n_ctx) + ")");
            return false;
        }

        // Each call starts from an empty KV cache so results do not depend on earlier calls.
        llama_memory_clear(llama_get_memory(ctx), true);

        llama_batch batch = llama_batch_get_one(prompt_tokens.data(),
                                                static_cast<int32_t>(prompt_tokens.size()));
        if (llama_decode(ctx, batch) != 0) {
            set_error(error, "prompt decode failed");
            return false;
        }

        llama_sampler* greedy = llama_sampler_init_greedy();
        std::string text;
        char piece[256];
        for (int i = 0; i < opt.max_new_tokens; ++i) {
            const llama_token tok = llama_sampler_sample(greedy, ctx, -1);
            if (llama_vocab_is_eog(vocab, tok)) break;

            int32_t n = llama_token_to_piece(vocab, tok, piece, sizeof(piece), 0, false);
            if (n < 0) {
                // Piece longer than the buffer: retry with the exact size.
                std::vector<char> big(static_cast<size_t>(-n));
                n = llama_token_to_piece(vocab, tok, big.data(), static_cast<int32_t>(big.size()), 0,
                                         false);
                if (n > 0) text.append(big.data(), static_cast<size_t>(n));
            } else if (n > 0) {
                text.append(piece, static_cast<size_t>(n));
            }

            llama_token next = tok;
            batch = llama_batch_get_one(&next, 1);
            if (llama_decode(ctx, batch) != 0) {
                llama_sampler_free(greedy);
                set_error(error, "generation decode failed");
                return false;
            }
        }
        llama_sampler_free(greedy);

        *out = std::move(text);
        return true;
    }
};

Model::Model(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Model::~Model() = default;

std::unique_ptr<Model> Model::load(const std::string& gguf_path, const Options& opt,
                                   std::string* error) {
    backend_once();

    auto impl = std::make_unique<Impl>();
    impl->opt = opt;

    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = opt.n_gpu_layers;
    impl->model = llama_model_load_from_file(gguf_path.c_str(), mparams);
    if (!impl->model) {
        set_error(error, "could not load model: " + gguf_path);
        return nullptr;
    }

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx = static_cast<uint32_t>(opt.n_ctx);
    // The whole prompt goes through one batch, so the batch must hold a full context.
    cparams.n_batch = static_cast<uint32_t>(opt.n_ctx);
    cparams.n_threads = opt.n_threads;
    cparams.n_threads_batch = opt.n_threads;
    impl->ctx = llama_init_from_model(impl->model, cparams);
    if (!impl->ctx) {
        set_error(error, "could not create a context for: " + gguf_path);
        return nullptr;
    }
    impl->vocab = llama_model_get_vocab(impl->model);
    return std::unique_ptr<Model>(new Model(std::move(impl)));
}

bool Model::complete(const std::string& prompt, std::string* out, std::string* error) {
    return impl_->run(prompt, out, error);
}

bool Model::chat(const std::string& system, const std::string& user, std::string* out,
                 std::string* error) {
    const std::string prompt = "<|im_start|>system\n" + system + "<|im_end|>\n<|im_start|>user\n" +
                               user + "<|im_end|>\n<|im_start|>assistant\n";
    return impl_->run(prompt, out, error);
}

}  // namespace wb::llm
