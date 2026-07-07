#include "voiceqas/stt/client.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <unordered_map>

#include "sherpa-onnx/c-api/c-api.h"

#include "voiceqas/ports/pipeline_telemetry.hpp"
#include "voiceqas/stt/provider.hpp"
#include "voiceqas/stt/vad_model.hpp"

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
        {"turbo-encoder.int8.onnx",
         "turbo-encoder.onnx",
         "large-v3-turbo-encoder.onnx",
         "whisper-turbo-encoder.onnx",
         "encoder.onnx"});
    paths.decoder = first_existing(
        dir,
        {"turbo-decoder.int8.onnx",
         "turbo-decoder.onnx",
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

SherpaOnnxOfflineRecognizerConfig make_base_config(int num_threads, const std::string& provider) {
    SherpaOnnxOfflineRecognizerConfig config{};
    config.feat_config.sample_rate = 16000;
    config.feat_config.feature_dim = 80;
    config.model_config.num_threads = num_threads;
    config.model_config.debug = 0;
    config.model_config.provider = provider.c_str();
    config.decoding_method = "greedy_search";
    config.max_active_paths = 4;
    return config;
}

const SherpaOnnxOfflineRecognizer* create_parakeet_recognizer(
    const ModelPaths& paths,
    int num_threads,
    const std::string& provider,
    std::string& error) {
    auto config = make_base_config(num_threads, provider);
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
    const std::string& provider,
    std::string& error) {
    auto config = make_base_config(num_threads, provider);
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

struct SttEngine::RecognizerBundle {
    std::string provider;
    const SherpaOnnxOfflineRecognizer* parakeet = nullptr;
    const SherpaOnnxOfflineRecognizer* whisper = nullptr;

    ~RecognizerBundle() {
        if (parakeet) {
            SherpaOnnxDestroyOfflineRecognizer(parakeet);
        }
        if (whisper) {
            SherpaOnnxDestroyOfflineRecognizer(whisper);
        }
    }
};

struct SttEngine::Impl {
    ModelPaths parakeet_paths;
    ModelPaths whisper_paths;
    bool parakeet_resolved = false;
    bool whisper_resolved = false;
    std::string parakeet_dir;
    std::string whisper_dir;
    mutable std::unordered_map<std::string, std::unique_ptr<RecognizerBundle>> bundles;
    std::unique_ptr<SileroVad> vad;
    std::string vad_model_id;
    std::string vad_model_name;
    std::string load_error;
};

SttEngine::SttEngine(
    SttConfig config,
    std::shared_ptr<ports::IPipelineTelemetry> telemetry)
    : config_(std::move(config)),
      telemetry_(std::move(telemetry)),
      impl_(std::make_unique<Impl>()) {
    if (!config_.enabled) {
        return;
    }

    config_.provider = normalize_stt_provider(config_.provider);

    impl_->parakeet_dir = config_.parakeet_dir.empty()
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
    impl_->whisper_dir = whisper_dir;

    if (const auto paths = resolve_parakeet_paths(impl_->parakeet_dir)) {
        impl_->parakeet_paths = *paths;
        impl_->parakeet_resolved = true;
    } else {
        impl_->load_error = "parakeet model directory not found: " + impl_->parakeet_dir;
        std::cerr << "voiceqas STT warning: " << impl_->load_error << '\n';
    }

    if (const auto paths = resolve_whisper_paths(impl_->whisper_dir)) {
        impl_->whisper_paths = *paths;
        if (!config_.whisper_callcenter_dir.empty() && whisper_dir == config_.whisper_callcenter_dir) {
            impl_->whisper_paths.label = "whisper-large-v3-turbo-ptbr-callcenter";
        }
        impl_->whisper_resolved = true;
    } else {
        const auto msg = "whisper model directory not found: " + whisper_dir;
        if (impl_->load_error.empty()) {
            impl_->load_error = msg;
        }
        std::cerr << "voiceqas STT warning: " << msg << '\n';
    }

    load_provider_locked(config_.provider);

    if (config_.vad.enabled) {
        const auto resolved = resolve_vad_model(
            config_.models_dir, config_.vad.model, config_.vad.model_path);
        if (resolved.path.empty()) {
            std::cerr << "voiceqas STT warning: no Silero VAD model found under "
                      << config_.models_dir << " (selector=" << config_.vad.model << ")\n";
        } else {
            auto vad_cfg = config_.vad;
            vad_cfg.model_path = resolved.path;
            if (vad_cfg.provider.empty()) {
                vad_cfg.provider = config_.provider;
            } else {
                vad_cfg.provider = normalize_stt_provider(vad_cfg.provider);
            }
            impl_->vad_model_id = resolved.id;
            impl_->vad_model_name = resolved.name;
            impl_->vad = std::make_unique<SileroVad>(vad_cfg);
            if (impl_->vad->ready()) {
                std::cerr << "voiceqas STT: silero VAD [" << resolved.id << "] loaded from "
                          << resolved.path << " (provider=" << vad_cfg.provider << ")\n";
            } else {
                std::cerr << "voiceqas STT warning: silero VAD unavailable at " << resolved.path
                          << '\n';
                impl_->vad.reset();
            }
        }
    }
}

SttEngine::~SttEngine() = default;

void SttEngine::load_provider_locked(const std::string& provider) const {
    const auto normalized = normalize_stt_provider(provider);
    if (impl_->bundles.contains(normalized)) {
        return;
    }

    auto bundle = std::make_unique<RecognizerBundle>();
    bundle->provider = normalized;

    if (impl_->parakeet_resolved) {
        std::string err;
        bundle->parakeet = create_parakeet_recognizer(
            impl_->parakeet_paths, config_.num_threads, normalized, err);
        if (bundle->parakeet) {
            std::cerr << "voiceqas STT: parakeet [" << normalized << "] loaded from "
                      << impl_->parakeet_dir << '\n';
        } else if (!err.empty()) {
            std::cerr << "voiceqas STT warning: " << err << " (provider=" << normalized << ")\n";
        }
    }

    if (impl_->whisper_resolved) {
        std::string err;
        bundle->whisper = create_whisper_recognizer(
            impl_->whisper_paths, config_.num_threads, config_.language, normalized, err);
        if (bundle->whisper) {
            std::cerr << "voiceqas STT: whisper [" << normalized << "] loaded from "
                      << impl_->whisper_dir << '\n';
        } else if (!err.empty()) {
            std::cerr << "voiceqas STT warning: " << err << " (provider=" << normalized << ")\n";
        }
    }

    impl_->bundles.emplace(normalized, std::move(bundle));
}

SttEngine::RecognizerBundle& SttEngine::ensure_provider_locked(const std::string& provider) const {
    const auto normalized = normalize_stt_provider(provider);
    load_provider_locked(normalized);
    return *impl_->bundles.at(normalized);
}

SttReadyStatus SttEngine::ready_status() const {
    std::lock_guard lock(mutex_);
    SttReadyStatus status;
    status.provider = normalize_stt_provider(config_.provider);
    status.cuda_compiled = stt_cuda_compiled();
    status.providers_available = available_stt_providers();
    for (const auto& [provider, bundle] : impl_->bundles) {
        (void)bundle;
        status.loaded_providers.push_back(provider);
    }

    const auto& active = ensure_provider_locked(status.provider);
    status.parakeet_ready = active.parakeet != nullptr;
    status.whisper_ready = active.whisper != nullptr;
    status.parakeet_model = impl_->parakeet_paths.label;
    status.whisper_model = impl_->whisper_paths.label;
    status.vad_ready = impl_->vad && impl_->vad->ready();
    status.vad_model = impl_->vad_model_name;
    status.vad_model_id = impl_->vad_model_id;
    return status;
}

bool SttEngine::reload_vad(const std::string& model_selector) {
    if (!config_.vad.enabled) {
        return false;
    }
    std::lock_guard lock(mutex_);
    const auto resolved = resolve_vad_model(config_.models_dir, model_selector, {});
    if (resolved.path.empty()) {
        return false;
    }
    config_.vad.model = model_selector;
    auto vad_cfg = config_.vad;
    vad_cfg.model_path = resolved.path;
    if (vad_cfg.provider.empty()) {
        vad_cfg.provider = config_.provider;
    }
    auto next = std::make_unique<SileroVad>(vad_cfg);
    if (!next->ready()) {
        return false;
    }
    impl_->vad = std::move(next);
    impl_->vad_model_id = resolved.id;
    impl_->vad_model_name = resolved.name;
    std::cerr << "voiceqas STT: silero VAD reloaded [" << resolved.id << "] from " << resolved.path
              << '\n';
    return true;
}

std::string SttEngine::active_vad_model_id() const {
    std::lock_guard lock(mutex_);
    return impl_->vad_model_id;
}

TranscriptResult SttEngine::transcribe_pcm16(
    std::span<const int16_t> pcm,
    int sample_rate,
    TranscribeOptions options) {
    TranscriptResult failure;
    if (!config_.enabled) {
        failure.error = "STT disabled";
        return failure;
    }

    const auto language = options.language.empty() ? config_.language : options.language;
    const auto choice = options.model;
    const auto provider = normalize_stt_provider(options.provider.value_or(config_.provider));
    if (const auto provider_error = validate_stt_provider(provider); !provider_error.empty()) {
        failure.error = provider_error;
        return failure;
    }

    std::lock_guard lock(mutex_);
    auto& bundle = ensure_provider_locked(provider);

    std::vector<int16_t> working(pcm.begin(), pcm.end());
    int rate = sample_rate;
    const auto bytes_in = working.size() * sizeof(int16_t);
    const bool apply_vad =
        options.apply_vad.value_or(config_.vad.enabled && config_.vad.apply_before_stt);
    if (apply_vad && impl_->vad && impl_->vad->ready()) {
        const auto vad_started = std::chrono::steady_clock::now();
        working = impl_->vad->extract_speech(working, rate);
        rate = 16000;
        const auto vad_ms = std::chrono::duration<double, std::milli>(
                                std::chrono::steady_clock::now() - vad_started)
                                .count();
        if (options.telemetry_session_id) {
            telemetry_->record_vad(
                *options.telemetry_session_id,
                vad_ms,
                bytes_in,
                working.size() * sizeof(int16_t));
        }
        if (working.empty()) {
            TranscriptResult empty;
            empty.ok = true;
            empty.model = "vad-filtered";
            empty.language = language;
            return empty;
        }
    }

    auto try_parakeet = [&]() -> TranscriptResult {
        if (!bundle.parakeet) {
            TranscriptResult unavailable;
            unavailable.error = "parakeet model unavailable for provider " + provider;
            return unavailable;
        }
        return decode_with_recognizer(
            bundle.parakeet, impl_->parakeet_paths.label, language, working, rate);
    };

    auto try_whisper = [&]() -> TranscriptResult {
        if (!bundle.whisper) {
            TranscriptResult unavailable;
            unavailable.error = "whisper model unavailable for provider " + provider;
            return unavailable;
        }
        return decode_with_recognizer(
            bundle.whisper, impl_->whisper_paths.label, language, working, rate);
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
