add_library(dkc1_android SHARED
    ${DKC1_ENGINE_SOURCES}
    ${DKC1_SNESRECOMP_GEN_SOURCES}
    ${DKC1_RUNNER_SOURCES}
    runner/headless_host.c
    runner/android/android_jni.c
    runner/android/android_audio.c
    runner/android/android_saves.c
)

target_include_directories(dkc1_android PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/runner
    ${CMAKE_CURRENT_SOURCE_DIR}/recomp
    ${SNESRECOMP_RUNNER_INCLUDE_DIRS}
)

target_compile_definitions(dkc1_android PRIVATE
    SNESRECOMP_TRACE=0
    SNESRECOMP_REVERSE_DEBUG=0
    SNESRECOMP_EXTERNAL_RAM_ROUTINE_GUARDS=1
    SYSTEM_VOLUME_MIXER_AVAILABLE=0
    DKC1_BUILD_CONFIG="android"
)

target_link_libraries(dkc1_android PRIVATE
    ${SNESRECOMP_RUNNER_LIBRARIES}
    android
    log
    aaudio
)

# Gradle debug variants configure CMake with CMAKE_BUILD_TYPE=Debug (-O0),
# which is far too slow for the recompiled game code and the software PPU.
# Keep the debug info but optimize; release variants already optimize.
target_compile_options(dkc1_android PRIVATE $<$<CONFIG:Debug>:-O2>)

set_target_properties(dkc1_android PROPERTIES
    OUTPUT_NAME "dkc1_android"
)
