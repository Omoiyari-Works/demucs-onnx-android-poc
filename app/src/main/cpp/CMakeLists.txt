cmake_minimum_required(VERSION 3.22.1)
project("demucspoc")

if(NOT DEFINED ONNXRUNTIME_DIR)
    message(FATAL_ERROR "ONNXRUNTIME_DIR not set (should be passed by app/build.gradle)")
endif()

add_library(onnxruntime SHARED IMPORTED)
set_target_properties(onnxruntime PROPERTIES
    IMPORTED_LOCATION ${ONNXRUNTIME_DIR}/jni/${ANDROID_ABI}/libonnxruntime.so)

add_library(demucspoc SHARED
    native-lib.cpp
    demucs_separator.cpp
    wav_io.cpp)

target_compile_features(demucspoc PRIVATE cxx_std_20)

target_include_directories(demucspoc PRIVATE
    ${ONNXRUNTIME_DIR}/headers)

find_library(log-lib log)
find_library(android-lib android)

target_link_libraries(demucspoc
    onnxruntime
    ${android-lib}
    ${log-lib})
