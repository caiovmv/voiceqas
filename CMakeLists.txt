cmake_minimum_required(VERSION 3.29)
project(voiceqas VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# C++26 só nos targets voiceqas — não propagar para sherpa-onnx/openfst (incompatível).
function(voiceqas_apply_cxx_std target)
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=c++26>)
endfunction()

option(VOICEQAS_BUILD_TESTS "Build unit tests" ON)
option(VOICEQAS_ENABLE_STT "Embed sherpa-onnx STT engine" ON)

include(FetchContent)
if(VOICEQAS_ENABLE_STT)
    set(SHERPA_ONNX_ENABLE_BINARY OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_PYTHON OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_TTS OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_SPEAKER_DIARIZATION OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_PORTAUDIO OFF CACHE BOOL "" FORCE)
    set(SHERPA_ONNX_ENABLE_WEBSOCKET OFF CACHE BOOL "" FORCE)
    # Shared onnxruntime evita duplicar RE2/protobuf (grpc vcpkg + ORT estático).
    set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)

    FetchContent_Declare(
        sherpa-onnx
        GIT_REPOSITORY https://github.com/k2-fsa/sherpa-onnx.git
        GIT_TAG v1.12.0
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
)
set(PROTO_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PROTO_OUT_DIR})

set(PROTO_SRCS
    ${PROTO_OUT_DIR}/voice_quality.pb.cc
    ${PROTO_OUT_DIR}/stt.pb.cc
)
set(PROTO_HDRS
    ${PROTO_OUT_DIR}/voice_quality.pb.h
    ${PROTO_OUT_DIR}/stt.pb.h
)
set(GRPC_SRCS
    ${PROTO_OUT_DIR}/voice_quality.grpc.pb.cc
    ${PROTO_OUT_DIR}/stt.grpc.pb.cc
)
set(GRPC_HDRS
    ${PROTO_OUT_DIR}/voice_quality.grpc.pb.h
    ${PROTO_OUT_DIR}/stt.grpc.pb.h
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

add_subdirectory(third_party/spandsp)

add_library(voiceqas_core STATIC
    src/core/metrics.cpp
    src/core/stt_gate.cpp
    src/core/analyzer.cpp
    src/core/json_util.cpp
    src/core/config.cpp
    src/core/wav.cpp
    src/rtp/depacketizer.cpp
    src/rtp/g722_codec.cpp
    src/rtp/g729_codec.cpp
    src/stt/audio_prepare.cpp
    src/stt/engine.cpp
    src/stt/model_util.cpp
    src/stt/json_util.cpp
    src/stt/session_manager.cpp
)
voiceqas_apply_cxx_std(voiceqas_core)
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
)
if(VOICEQAS_ENABLE_STT)
    target_compile_definitions(voiceqas_core PUBLIC VOICEQAS_ENABLE_STT=1)
    target_link_libraries(voiceqas_core PUBLIC sherpa-onnx-c-api)
    target_include_directories(voiceqas_core PUBLIC
        ${sherpa-onnx_SOURCE_DIR}
    )
endif()

add_library(voiceqas_server STATIC
    src/server/rest_server.cpp
    src/server/grpc_server.cpp
    src/server/ws_server.cpp
)
voiceqas_apply_cxx_std(voiceqas_server)
target_include_directories(voiceqas_server PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROTO_OUT_DIR}
)
target_link_libraries(voiceqas_server PUBLIC
    voiceqas_core
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
    )
    voiceqas_apply_cxx_std(voiceqas_tests)
    target_link_libraries(voiceqas_tests PRIVATE voiceqas_core GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(voiceqas_tests)
endif()

install(TARGETS voiceqas-server RUNTIME DESTINATION bin)
install(DIRECTORY config/ DESTINATION etc/voiceqas)
install(DIRECTORY web/ DESTINATION share/voiceqas/web)
install(FILES openapi/voiceqas.yaml DESTINATION share/voiceqas/openapi)
