#include "voiceqas/stt/client.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>

#include "sherpa-onnx/c-api/c-api.h"

namespace voiceqas::stt {

namespace {

namespace fs = std::filesystem;

std::string trim_copy(std::string value) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::string join_path(const std::string& base, const std::string& leaf) {
    return (fs::path(base) / leaf).string();
}

std::string first_existing(const std::string& dir, std::initializer_list<const char*> names) {
    for (const char* name : names) {
        const auto path = join_path(dir, name);
        if (fs::exists(path)) {
            return path;
        }
    }
    return names.size() != 0 ? join_path(dir, *names.begin()) : dir;
}

struct ModelPaths {
    std::string encoder;
    std::string decoder;
    std::string joiner;
    std::string tokens;
    std::string label;
};

std::optional<ModelPaths> resolve_parakeet_paths(const std::string& dir) {
    if (dir.empty() || !fs::is_directory(dir)) {
        return std::nullopt;
    }
    ModelPaths paths;
    paths.encoder = first_existing(dir, {"encoder.int8.onnx", "encoder.onnx"});
    paths.decoder = first_existing(dir, {"decoder.int8.onnx", "decoder.onnx"});
    paths.joiner = first_existing(dir, {"joiner.int8.onnx", "joiner.onnx"});
    paths.tokens = first_existing(dir, {"tokens.txt"});
    paths.label = "parakeet-tdt-0.6b-v3-int8";
    if (!fs::exists(paths.encoder) || !fs::exists(paths.decoder) || !fs::exists(paths.joiner) ||
        !fs::exists(paths.tokens)) {
        return std::nullopt;
    }
    return paths;
}

std::optional<ModelPaths> resolve_whisper_paths(const std::string& dir) {
    if (dir.empty() || !fs::is_directory(dir)) {
        return std::nullopt;
    }
    ModelPaths paths;
    paths.encoder = first_existing(
        dir,
        {"turbo-encoder.onnx",
         "large-v3-turbo-encoder.onnx",
         "whisper-turbo-encoder.onnx",
         "encoder.onnx"});
    paths.decoder = first_existing(
        dir,
        {"turbo-decoder.onnx",
         "large-v3-turbo-decoder.onnx",
         "whisper-turbo-decoder.onnx",
         "decoder.onnx"});
    paths.tokens = first_existing(dir, {"turbo-tokens.txt", "tokens.txt"});
    paths.label = "whisper-large-v3-turbo-pt";
    if (!fs::exists(paths.encoder) || !fs::exists(paths.decoder) || !fs::exists(paths.tokens)) {
        return std::nullopt;
    }
    return paths;
}

SherpaOnnxOfflineRecognizerConfig make_base_config(int num_threads) {
    SherpaOnnxOfflineRecognizerConfig config{};
    config.feat_config.sample_rate = 16000;
    config.feat_config.feature_dim = 80;
    config.model_config.num_threads = num_threads;
    config.model_config.debug = 0;
    config.model_config.provider = "cpu";
    config.decoding_method = "greedy_search";
    config.max_active_paths = 4;
    return config;
}

const SherpaOnnxOfflineRecognizer* create_parakeet_recognizer(
    const ModelPaths& paths,
    int num_threads,
    std::string& error) {
    auto config = make_base_config(num_threads);
    config.model_config.transducer.encoder = paths.encoder.c_str();
    config.model_config.transducer.decoder = paths.decoder.c_str();
    config.model_config.transducer.joiner = paths.joiner.c_str();
    config.model_config.tokens = paths.tokens.c_str();
    config.model_config.model_type = "nemo_transducer";

    const auto* recognizer = SherpaOnnxCreateOfflineRecognizer(&config);
    if (!recognizer) {
        error = "failed to load parakeet model from " + paths.encoder;
    }
    return recognizer;
}

const SherpaOnnxOfflineRecognizer* create_whisper_recognizer(
    const ModelPaths& paths,
    int num_threads,
    const std::string& language,
    std::string& error) {
    auto config = make_base_config(num_threads);
    config.model_config.whisper.encoder = paths.encoder.c_str();
    config.model_config.whisper.decoder = paths.decoder.c_str();
    config.model_config.whisper.language = language.c_str();
    config.model_config.whisper.task = "transcribe";
    config.model_config.whisper.tail_paddings = -1;
    config.model_config.tokens = paths.tokens.c_str();
    config.model_config.model_type = "whisper";

    const auto* recognizer = SherpaOnnxCreateOfflineRecognizer(&config);
    if (!recognizer) {
        error = "failed to load whisper model from " + paths.encoder;
    }
    return recognizer;
}

TranscriptResult decode_with_recognizer(
    const SherpaOnnxOfflineRecognizer* recognizer,
    const std::string& model_label,
    const std::string& language,
    std::span<const int16_t> pcm,
    int sample_rate) {
    TranscriptResult result;
    result.model = model_label;
    result.language = language;
    result.duration_ms = pcm.empty() ? 0 : static_cast<int64_t>(pcm.size()) * 1000 / sample_rate;

    if (!recognizer) {
        result.error = "recognizer unavailable";
        return result;
    }
    if (pcm.empty()) {
        result.ok = true;
        return result;
    }

    const auto started = std::chrono::steady_clock::now();
    const auto* stream = SherpaOnnxCreateOfflineStream(recognizer);
    if (!stream) {
        result.error = "failed to create offline stream";
        return result;
    }

    std::vector<float> samples(pcm.size());
    for (size_t i = 0; i < pcm.size(); ++i) {
        samples[i] = static_cast<float>(pcm[i]) / 32768.0f;
    }

    SherpaOnnxAcceptWaveformOffline(stream, sample_rate, samples.data(), static_cast<int32_t>(samples.size()));
    SherpaOnnxDecodeOfflineStream(recognizer, stream);

    const auto* sherpa_result = SherpaOnnxGetOfflineStreamResult(stream);
    if (sherpa_result && sherpa_result->text) {
        result.text = trim_copy(sherpa_result->text);
        result.ok = true;
    } else {
        result.error = "empty recognition result";
    }

    if (sherpa_result) {
        SherpaOnnxDestroyOfflineRecognizerResult(sherpa_result);
    }
    SherpaOnnxDestroyOfflineStream(stream);

    result.processing_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - started)
                               .count();
    return result;
}

}  // namespace

SttModelChoice parse_model_choice(const std::string& value, SttModelChoice fallback) {
    const auto normalized = [&]() {
        std::string lower = value;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return lower;
    }();

    if (normalized == "parakeet") {
        return SttModelChoice::Parakeet;
    }
    if (normalized == "whisper" || normalized == "whisper_callcenter" || normalized == "callcenter") {
        return SttModelChoice::Whisper;
    }
    if (normalized == "auto") {
        return SttModelChoice::Auto;
    }
    return fallback;
}

struct SttEngine::Impl {
    const SherpaOnnxOfflineRecognizer* parakeet = nullptr;
    const SherpaOnnxOfflineRecognizer* whisper = nullptr;
    ModelPaths parakeet_paths;
    ModelPaths whisper_paths;
    std::string load_error;
};

SttEngine::SttEngine(SttConfig config) : config_(std::move(config)), impl_(std::make_unique<Impl>()) {
    if (!config_.enabled) {
        return;
    }

    const auto parakeet_dir = config_.parakeet_dir.empty()
        ? join_path(config_.models_dir, "sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8")
        : config_.parakeet_dir;

    std::string whisper_dir = config_.whisper_dir;
    if (whisper_dir.empty()) {
        if (!config_.whisper_callcenter_dir.empty() && fs::is_directory(config_.whisper_callcenter_dir)) {
            whisper_dir = config_.whisper_callcenter_dir;
        } else {
            whisper_dir = join_path(config_.models_dir, "sherpa-onnx-whisper-turbo");
        }
    }

    if (const auto paths = resolve_parakeet_paths(parakeet_dir)) {
        impl_->parakeet_paths = *paths;
        impl_->parakeet = create_parakeet_recognizer(*paths, config_.num_threads, impl_->load_error);
        if (impl_->parakeet) {
            std::cerr << "voiceqas STT: parakeet loaded from " << parakeet_dir << '\n';
        }
    } else {
        impl_->load_error = "parakeet model directory not found: " + parakeet_dir;
        std::cerr << "voiceqas STT warning: " << impl_->load_error << '\n';
    }

    if (const auto paths = resolve_whisper_paths(whisper_dir)) {
        impl_->whisper_paths = *paths;
        if (!config_.whisper_callcenter_dir.empty() && whisper_dir == config_.whisper_callcenter_dir) {
            impl_->whisper_paths.label = "whisper-large-v3-turbo-ptbr-callcenter";
        }
        impl_->whisper = create_whisper_recognizer(
            impl_->whisper_paths, config_.num_threads, config_.language, impl_->load_error);
        if (impl_->whisper) {
            std::cerr << "voiceqas STT: whisper loaded from " << whisper_dir << '\n';
        }
    } else {
        const auto msg = "whisper model directory not found: " + whisper_dir;
        if (impl_->load_error.empty()) {
            impl_->load_error = msg;
        }
        std::cerr << "voiceqas STT warning: " << msg << '\n';
    }
}

SttEngine::~SttEngine() {
    if (impl_->parakeet) {
        SherpaOnnxDestroyOfflineRecognizer(impl_->parakeet);
    }
    if (impl_->whisper) {
        SherpaOnnxDestroyOfflineRecognizer(impl_->whisper);
    }
}

SttReadyStatus SttEngine::ready_status() const {
    SttReadyStatus status;
    status.parakeet_ready = impl_->parakeet != nullptr;
    status.whisper_ready = impl_->whisper != nullptr;
    status.parakeet_model = impl_->parakeet_paths.label;
    status.whisper_model = impl_->whisper_paths.label;
    return status;
}

TranscriptResult SttEngine::transcribe_pcm16(
    std::span<const int16_t> pcm,
    int sample_rate,
    TranscribeOptions options) const {
    TranscriptResult failure;
    if (!config_.enabled) {
        failure.error = "STT disabled";
        return failure;
    }

    const auto language = options.language.empty() ? config_.language : options.language;
    const auto choice = options.model;

    std::lock_guard lock(mutex_);

    auto try_parakeet = [&]() -> TranscriptResult {
        if (!impl_->parakeet) {
            TranscriptResult unavailable;
            unavailable.error = "parakeet model unavailable";
            return unavailable;
        }
        return decode_with_recognizer(
            impl_->parakeet, impl_->parakeet_paths.label, language, pcm, sample_rate);
    };

    auto try_whisper = [&]() -> TranscriptResult {
        if (!impl_->whisper) {
            TranscriptResult unavailable;
            unavailable.error = "whisper model unavailable";
            return unavailable;
        }
        return decode_with_recognizer(
            impl_->whisper, impl_->whisper_paths.label, language, pcm, sample_rate);
    };

    if (choice == SttModelChoice::Parakeet) {
        return try_parakeet();
    }
    if (choice == SttModelChoice::Whisper) {
        return try_whisper();
    }

    auto primary = try_parakeet();
    if (primary.ok && !trim_copy(primary.text).empty()) {
        return primary;
    }

    auto fallback = try_whisper();
    if (fallback.ok) {
        if (!primary.text.empty() && trim_copy(fallback.text).empty()) {
            fallback.text = primary.text;
        }
        if (!primary.error.empty() && trim_copy(fallback.text).empty()) {
            fallback.error = primary.error + "; whisper fallback also empty";
        }
        return fallback;
    }

    if (!primary.error.empty()) {
        primary.error += "; whisper fallback failed: " + fallback.error;
    } else {
        primary.error = fallback.error;
    }
    return primary;
}

}  // namespace voiceqas::stt
