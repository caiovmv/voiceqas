cmake_minimum_required(VERSION 3.29)
project(voiceqas VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# C++26 só nos targets voiceqas — não propagar para sherpa-onnx/openfst (incompatível).
function(voiceqas_apply_cxx_std target)
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=c++26>)
    if(VOICEQAS_PERF_BADASS)
        target_compile_options(${target} PRIVATE
            $<$<AND:$<COMPILE_LANGUAGE:CXX>,$<NOT:$<CONFIG:Debug>>>:-march=x86-64-v3>
        )
        set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    endif()
endfunction()

function(voiceqas_apply_badass_c target)
    if(VOICEQAS_PERF_BADASS)
        target_compile_options(${target} PRIVATE $<$<NOT:$<CONFIG:Debug>>:-march=x86-64-v3>)
        set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    endif()
endfunction()

option(VOICEQAS_BUILD_TESTS "Build unit tests" ON)
option(VOICEQAS_PERF_BADASS "Release tuning: LTO + x86-64-v3 on voiceqas targets" OFF)
option(VOICEQAS_ENABLE_STT "Embed sherpa-onnx STT engine" ON)
option(VOICEQAS_STT_CUDA "Build sherpa-onnx with CUDA ONNX Runtime provider" OFF)
option(VOICEQAS_ENABLE_RNNOISE "Link RNNoise for audio enhancement (Linux/x64 via vcpkg)" ON)
option(VOICEQAS_COVERAGE "Enable gcov coverage instrumentation" OFF)

function(voiceqas_enable_coverage target)
    if(VOICEQAS_COVERAGE)
        target_compile_options(${target} PRIVATE --coverage -O0 -g)
        target_link_options(${target} PRIVATE --coverage)
    endif()
endfunction()

include(FetchContent)
if(VOICEQAS_ENABLE_STT)
    set(SHERPA_ONNX_ENABLE_BINARY OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_PYTHON OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_TTS OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_PORTAUDIO OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_WEBSOCKET OFF CACHE BOOL "" FORCE)
    if(VOICEQAS_STT_CUDA)
        set(SHERPA_ONNX_ENABLE_GPU ON CACHE BOOL "" FORCE)
    endif()
    # Shared onnxruntime evita duplicar RE2/protobuf (grpc vcpkg + ORT estático).
    set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)

    FetchContent_Declare(
        sherpa-onnx
        GIT_REPOSITORY https://github.com/k2-fsa/sherpa-onnx.git
        GIT_TAG v1.13.3
    )
    FetchContent_MakeAvailable(sherpa-onnx)
endif()

find_package(nlohmann_json CONFIG REQUIRED)
find_package(httplib CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)
find_package(Bcg729 CONFIG REQUIRED)
if(VOICEQAS_BUILD_TESTS)
    find_package(GTest CONFIG REQUIRED)
endif()

set(PROTO_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/voice_quality.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/stt.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/media.proto
)
set(PROTO_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PROTO_OUT_DIR})

set(PROTO_SRCS
    ${PROTO_OUT_DIR}/voice_quality.pb.cc
    ${PROTO_OUT_DIR}/stt.pb.cc
    ${PROTO_OUT_DIR}/media.pb.cc
)
set(PROTO_HDRS
    ${PROTO_OUT_DIR}/voice_quality.pb.h
    ${PROTO_OUT_DIR}/stt.pb.h
    ${PROTO_OUT_DIR}/media.pb.h
)
set(GRPC_SRCS
    ${PROTO_OUT_DIR}/voice_quality.grpc.pb.cc
    ${PROTO_OUT_DIR}/stt.grpc.pb.cc
    ${PROTO_OUT_DIR}/media.grpc.pb.cc
)
set(GRPC_HDRS
    ${PROTO_OUT_DIR}/voice_quality.grpc.pb.h
    ${PROTO_OUT_DIR}/stt.grpc.pb.h
    ${PROTO_OUT_DIR}/media.grpc.pb.h
)

add_custom_command(
    OUTPUT ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
    COMMAND protobuf::protoc
    ARGS --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/proto
         --cpp_out=${PROTO_OUT_DIR}
         --grpc_out=${PROTO_OUT_DIR}
         --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
         ${PROTO_FILES}
    DEPENDS ${PROTO_FILES}
    COMMENT "Generating gRPC/protobuf sources"
)

add_library(voiceqas_proto STATIC ${PROTO_SRCS} ${GRPC_SRCS})
voiceqas_apply_cxx_std(voiceqas_proto)
target_include_directories(voiceqas_proto PUBLIC ${PROTO_OUT_DIR})
target_link_libraries(voiceqas_proto PUBLIC protobuf::libprotobuf gRPC::grpc++)

find_package(opentelemetry-cpp CONFIG REQUIRED COMPONENTS exporters_otlp_grpc)

add_library(voiceqas_tracing STATIC
    src/tracing/tracing.cpp
)
voiceqas_apply_cxx_std(voiceqas_tracing)
target_compile_definitions(voiceqas_tracing PUBLIC VOICEQAS_ENABLE_TRACING=1)
target_include_directories(voiceqas_tracing PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(voiceqas_tracing PUBLIC
    opentelemetry-cpp::api
    opentelemetry-cpp::otlp_grpc_exporter
    opentelemetry-cpp::trace
    opentelemetry-cpp::resources
)

add_subdirectory(third_party/spandsp)
voiceqas_apply_badass_c(spandsp_g722)

if(VOICEQAS_PERF_BADASS)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT _voiceqas_ipo_ok OUTPUT _voiceqas_ipo_err)
    if(NOT _voiceqas_ipo_ok)
        message(FATAL_ERROR "VOICEQAS_PERF_BADASS requires LTO: ${_voiceqas_ipo_err}")
    endif()
    message(STATUS "VOICEQAS_PERF_BADASS: LTO + -march=x86-64-v3 on voiceqas targets (Release)")
endif()

add_library(voiceqas_core STATIC
    src/core/metrics.cpp
    src/core/stt_gate.cpp
    src/core/analyzer.cpp
    src/core/json_util.cpp
    src/core/config.cpp
    src/core/wav.cpp
    src/ports/noop_ports.cpp
    src/audio/normalizer.cpp
    src/audio/rnnoise_enhancer.cpp
    src/audio/dsp/biquad.cpp
    src/audio/dsp/channel_strip.cpp
    src/audio/decoder.cpp
    src/audio/encoder.cpp
    src/audio/resampler.cpp
    src/audio/ingress_codec.cpp
    src/audio/codec_registry.cpp
    src/rtp/depacketizer.cpp
    src/rtp/g711_codec.cpp
    src/rtp/g722_codec.cpp
    src/rtp/g729_codec.cpp
    src/rtp/packetizer.cpp
    src/media/session_manager.cpp
    src/media/rtp_ingress.cpp
    src/config/channel_registry.cpp
    src/stt/audio_prepare.cpp
    src/stt/engine.cpp
    src/stt/model_util.cpp
    src/stt/provider.cpp
    src/stt/vad_model.cpp
    src/stt/json_util.cpp
    src/stt/session_manager.cpp
    src/stt/vad.cpp
    src/stt/diarization.cpp
)
voiceqas_apply_cxx_std(voiceqas_core)
voiceqas_enable_coverage(voiceqas_core)
# yaml-cpp 0.9.0: operator[]/key_to_string breaks under -std=c++26 (GCC 15).
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core/config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/config/channel_registry.cpp
    TARGET_DIRECTORY voiceqas_core
    PROPERTIES COMPILE_OPTIONS "-std=c++20"
)
target_include_directories(voiceqas_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
if(TARGET bcg729::bcg729)
    set(VOICEQAS_BCG729_LIB bcg729::bcg729)
elseif(TARGET bcg729-static)
    set(VOICEQAS_BCG729_LIB bcg729-static)
elseif(TARGET bcg729)
    set(VOICEQAS_BCG729_LIB bcg729)
else()
    message(FATAL_ERROR "bcg729 target not found (expected bcg729::bcg729, bcg729-static or bcg729)")
endif()

# vcpkg instala bcg729 estático (.a); headers exigem BCG729_STATIC nos consumidores C++.
target_compile_definitions(voiceqas_core PUBLIC BCG729_STATIC)

target_link_libraries(voiceqas_core PUBLIC
    nlohmann_json::nlohmann_json
    yaml-cpp::yaml-cpp
    spandsp_g722
    ${VOICEQAS_BCG729_LIB}
    voiceqas_tracing
)
if(VOICEQAS_ENABLE_RNNOISE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(RNNOISE IMPORTED_TARGET rnnoise)
    endif()
    if(TARGET PkgConfig::RNNOISE)
        target_compile_definitions(voiceqas_core PUBLIC VOICEQAS_HAS_RNNOISE=1)
        target_link_libraries(voiceqas_core PUBLIC PkgConfig::RNNOISE)
        message(STATUS "RNNoise: enabled (pkg-config)")
    else()
        find_path(RNNOISE_INCLUDE_DIR rnnoise.h)
        find_library(RNNOISE_LIBRARY rnnoise)
        if(RNNOISE_INCLUDE_DIR AND RNNOISE_LIBRARY)
            target_compile_definitions(voiceqas_core PUBLIC VOICEQAS_HAS_RNNOISE=1)
            target_include_directories(voiceqas_core PUBLIC ${RNNOISE_INCLUDE_DIR})
            target_link_libraries(voiceqas_core PUBLIC ${RNNOISE_LIBRARY})
            message(STATUS "RNNoise: enabled (${RNNOISE_LIBRARY})")
        else()
            message(STATUS "RNNoise: requested but not found — enhancement will no-op")
        endif()
    endif()
else()
    message(STATUS "RNNoise: disabled (VOICEQAS_ENABLE_RNNOISE=OFF)")
endif()
if(VOICEQAS_ENABLE_STT)
    target_compile_definitions(voiceqas_core PUBLIC VOICEQAS_ENABLE_STT=1)
    if(VOICEQAS_STT_CUDA)
        target_compile_definitions(voiceqas_core PUBLIC VOICEQAS_STT_CUDA=1)
    endif()
    target_link_libraries(voiceqas_core PUBLIC sherpa-onnx-c-api)
    target_include_directories(voiceqas_core PUBLIC
        ${sherpa-onnx_SOURCE_DIR}
    )
endif()

add_library(voiceqas_ops STATIC
    src/ops/metrics_hub.cpp
    src/ops/event_store.cpp
    src/ops/webhook.cpp
    src/ops/prometheus.cpp
    src/ops/pipeline_tracker.cpp
    src/ops/pipeline_snapshot.cpp
    src/ops/transport_sankey.cpp
    src/ops/external_ai_config.cpp
    src/ops/external_ai_service.cpp
    src/ops/adapters/ops_ports.cpp
)
voiceqas_apply_cxx_std(voiceqas_ops)
voiceqas_enable_coverage(voiceqas_ops)
target_include_directories(voiceqas_ops PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(voiceqas_ops PUBLIC
    voiceqas_core
    nlohmann_json::nlohmann_json
    httplib::httplib
)

add_library(voiceqas_server STATIC
    src/server/auth.cpp
    src/server/rest_server.cpp
    src/server/routes/register_routes.cpp
    src/server/routes/static_routes.cpp
    src/server/routes/vqa_routes.cpp
    src/server/routes/media_routes.cpp
    src/server/routes/ops_routes.cpp
    src/server/routes/stt_routes.cpp
    src/server/routes/analysis_routes.cpp
    src/server/routes/config_routes.cpp
    src/server/grpc_mappers.cpp
    src/server/grpc_playground_client.cpp
    src/server/grpc_server.cpp
    src/server/ws_server.cpp
    src/server/media_relay.cpp
)
voiceqas_apply_cxx_std(voiceqas_server)
voiceqas_enable_coverage(voiceqas_server)
target_include_directories(voiceqas_server PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROTO_OUT_DIR}
)
target_link_libraries(voiceqas_server PUBLIC
    voiceqas_core
    voiceqas_ops
    voiceqas_proto
    httplib::httplib
    Boost::system
    gRPC::grpc++
)

add_executable(voiceqas-server src/server/main.cpp)
voiceqas_apply_cxx_std(voiceqas-server)
target_link_libraries(voiceqas-server PRIVATE voiceqas_server)

if(VOICEQAS_BUILD_TESTS)
    enable_testing()
    add_executable(voiceqas_tests
        tests/test_stt_gate.cpp
        tests/test_analyzer.cpp
        tests/test_rtp_g711.cpp
        tests/test_rtp_g722_g729.cpp
        tests/test_wav.cpp
        tests/test_vad_model.cpp
        tests/test_ingress_codec.cpp
        tests/test_session_manager_ports.cpp
        tests/test_config.cpp
        tests/test_codec_registry.cpp
        tests/test_rtp_ingress.cpp
        tests/test_vqa_session_manager.cpp
        tests/test_stt_session_manager.cpp
        tests/test_audio_pipeline.cpp
        tests/test_stt_gate_extended.cpp
        tests/test_diarization.cpp
        tests/test_rnnoise.cpp
        tests/test_biquad.cpp
        tests/test_channel_strip.cpp
        tests/test_pipeline_tracker.cpp
        tests/test_transport_sankey.cpp
        tests/test_rtp_packetizer.cpp
        tests/test_ops_webhook.cpp
        tests/test_prometheus.cpp
    )
    voiceqas_apply_cxx_std(voiceqas_tests)
    voiceqas_enable_coverage(voiceqas_tests)
    target_include_directories(voiceqas_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(voiceqas_tests PRIVATE voiceqas_core voiceqas_ops GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(voiceqas_tests PROPERTIES LABELS "unit;core")

    add_executable(voiceqas_server_tests
        tests/test_auth.cpp
        tests/test_ops_event_store.cpp
        tests/test_grpc_mappers.cpp
    )
    voiceqas_apply_cxx_std(voiceqas_server_tests)
    voiceqas_enable_coverage(voiceqas_server_tests)
    target_include_directories(voiceqas_server_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(voiceqas_server_tests PRIVATE voiceqas_server GTest::gtest_main)
    gtest_discover_tests(voiceqas_server_tests PROPERTIES LABELS "unit;server")
endif()

install(TARGETS voiceqas-server RUNTIME DESTINATION bin)
install(DIRECTORY config/ DESTINATION etc/voiceqas)
install(DIRECTORY web/ DESTINATION share/voiceqas/web)
install(FILES openapi/voiceqas.yaml DESTINATION share/voiceqas/openapi)
