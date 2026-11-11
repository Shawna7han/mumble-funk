# Optional Phase-1 file adapter. Never a dependency of funk-voice-dsp.
find_path(OPUS_INCLUDE_DIR opus.h PATH_SUFFIXES opus)
find_library(OPUS_LIBRARY NAMES opus)
if(NOT OPUS_INCLUDE_DIR OR NOT OPUS_LIBRARY)
	message(FATAL_ERROR "VOICE_BUILD_PREVIEW requires libopus headers and library")
endif()

add_library(funk-voice-preview-support STATIC
	../src/OpusCodec.cpp ../src/WaveIO.cpp ../src/Preview.cpp)
target_include_directories(funk-voice-preview-support PUBLIC "${OPUS_INCLUDE_DIR}")
target_link_libraries(funk-voice-preview-support PUBLIC funk-voice-dsp "${OPUS_LIBRARY}")
voice_target_options(funk-voice-preview-support)

add_executable(funk-voice-preview ../src/main.cpp)
target_link_libraries(funk-voice-preview PRIVATE funk-voice-preview-support)
voice_target_options(funk-voice-preview)

if(VOICE_BUILD_TESTS)
	add_executable(voice-codec-test ../tests/codec_test.cpp)
	target_link_libraries(voice-codec-test PRIVATE funk-voice-preview-support)
	voice_target_options(voice-codec-test)
	add_test(NAME VoiceCodec COMMAND voice-codec-test)
	find_package(Python3 COMPONENTS Interpreter REQUIRED)
	add_test(NAME VoiceCli COMMAND "${Python3_EXECUTABLE}"
		"${CMAKE_CURRENT_SOURCE_DIR}/../tests/cli_test.py" $<TARGET_FILE:funk-voice-preview>)
endif()
