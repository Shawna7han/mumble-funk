cmake_minimum_required(VERSION 3.15)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	project(FunkAnalogVoice VERSION 0.1.0 LANGUAGES CXX)
	set(VOICE_TESTS_DEFAULT ON)
else()
	set(VOICE_TESTS_DEFAULT ${tests})
endif()

option(VOICE_BUILD_TESTS "Build the Phase-1 analog voice tests" ${VOICE_TESTS_DEFAULT})
option(VOICE_BUILD_PREVIEW "Build the offline WAV/Opus preview (requires libopus)" OFF)
option(VOICE_SANITIZERS "Enable address/undefined sanitizers for analog voice targets" OFF)

# Keep all compiler and sanitizer settings local to these targets.
function(voice_target_options target)
	set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
	if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
		target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
		if(VOICE_SANITIZERS)
			target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
			target_link_options(${target} PRIVATE -fsanitize=address,undefined)
		endif()
	endif()
endfunction()

# Exactly one DSP library, shared by tests, preview and future server adapters.
# No Qt, Opus, Python, WAV I/O or Murmur objects in its dependency graph.
add_library(funk-voice-dsp STATIC src/VoiceProcessor.cpp)
target_include_directories(funk-voice-dsp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(funk-voice-dsp PUBLIC cxx_std_17)
voice_target_options(funk-voice-dsp)

if(VOICE_BUILD_TESTS)
	enable_testing()
	add_executable(voice-dsp-test tests/dsp_test.cpp)
	target_link_libraries(voice-dsp-test PRIVATE funk-voice-dsp)
	voice_target_options(voice-dsp-test)
	add_test(NAME VoiceDsp COMMAND voice-dsp-test)
endif()

if(VOICE_BUILD_PREVIEW)
	add_subdirectory(offline)
endif()
