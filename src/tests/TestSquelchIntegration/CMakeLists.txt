add_executable(TestSquelchIntegration TestSquelchIntegration.cpp)
set_target_properties(TestSquelchIntegration PROPERTIES AUTOMOC ON)
target_link_libraries(TestSquelchIntegration PRIVATE shared Qt5::Test audio-feedback)
target_compile_definitions(TestSquelchIntegration PRIVATE MUMBLE_SERVER_PATH="$<TARGET_FILE:mumble-server>" SQUELCH_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets/audio")
add_dependencies(TestSquelchIntegration mumble-server)
# Keep the complete original suite and all timeout cases. One enlarged invocation
# exceeded 180s on a slow CI runner; separate groups retain the same per-group
# watchdog and all per-packet assertions without rerunning or skipping cases.
add_test(NAME TestSquelchIntegration COMMAND $<TARGET_FILE:TestSquelchIntegration>
    transport disabled boundaries reusedSession switches rapidHandoff)
add_test(NAME TestSquelchTimeoutEnd COMMAND $<TARGET_FILE:TestSquelchIntegration>
    timeoutEnd)
add_test(NAME TestSquelchTimeoutLifecycle COMMAND $<TARGET_FILE:TestSquelchIntegration>
    timeoutBoundaries timeoutSessionReuse timeoutHandoff)
set_tests_properties(TestSquelchIntegration TestSquelchTimeoutEnd TestSquelchTimeoutLifecycle PROPERTIES TIMEOUT 180)
