cmake_minimum_required(VERSION 3.20)
project(vikshep-sim VERSION 0.1.0 LANGUAGES C CXX)

# Release is the default build type: the build type is part of "same build" in the
# determinism promise (docs/HITS.md, section 9), and the output is recorded in
# provenance.json (build_type). Optimisation never includes -ffast-math or -Ofast, which
# would let the compiler change floating-point results.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (default: Release)" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Floating-point results must not depend on what the optimiser may do to them: refuse the options
# that let it change them. (tests/check_fast_math.py checks that this fails, and that the real
# compile commands contain none of them.) Done before anything is searched, so it fails at once.
foreach(_flags "${CMAKE_C_FLAGS}" "${CMAKE_CXX_FLAGS}" "${CMAKE_C_FLAGS_RELEASE}" "${CMAKE_CXX_FLAGS_RELEASE}"
               "${CMAKE_C_FLAGS_RELWITHDEBINFO}" "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
  if(_flags MATCHES "fast-math|Ofast|unsafe-math|finite-math-only|associative-math")
    message(FATAL_ERROR
      "-ffast-math, -Ofast and related options are not allowed: they change floating-point results "
      "(found in: ${_flags})")
  endif()
endforeach()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Geant4 REQUIRED)


find_package(CLI11 REQUIRED)
find_package(nlohmann_json REQUIRED)

# Facts about the installed Geant4, read from its CMake config.
if(Geant4_gdml_FOUND)
  set(VIKSHEP_GEANT4_GDML 1)
else()
  set(VIKSHEP_GEANT4_GDML 0)
endif()
if(Geant4_multithreaded_FOUND)
  set(VIKSHEP_GEANT4_MULTITHREADED 1)
else()
  set(VIKSHEP_GEANT4_MULTITHREADED 0)
endif()

if(CMAKE_BUILD_TYPE)
  set(VIKSHEP_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
else()
  set(VIKSHEP_BUILD_TYPE "multi-config")
endif()

configure_file(src/build_config.hh.in ${CMAKE_CURRENT_BINARY_DIR}/generated/build_config.hh @ONLY)

# nanoarrow (with its IPC component and the flatcc runtime), vendored in
# third_party/nanoarrow at one pinned commit. Compiled as C; warnings of third-party
# code are not ours to fix.
add_library(nanoarrow_vendored STATIC
  third_party/nanoarrow/src/nanoarrow.c
  third_party/nanoarrow/src/nanoarrow_ipc.c
  third_party/nanoarrow/src/flatcc.c
)
target_include_directories(nanoarrow_vendored PUBLIC third_party/nanoarrow/include)
target_compile_options(nanoarrow_vendored PRIVATE -w)
set_target_properties(nanoarrow_vendored PROPERTIES POSITION_INDEPENDENT_CODE ON)

# tiny_sha3 (MIT), vendored in third_party/tiny_sha3, for the OID.
add_library(tiny_sha3 STATIC third_party/tiny_sha3/sha3.c)
target_include_directories(tiny_sha3 PUBLIC third_party/tiny_sha3)
target_compile_options(tiny_sha3 PRIVATE -w)

# Everything except main() lives in a library so the tests can link it.
add_library(vikshep_core STATIC
  src/cmd_info.cc
  src/cmd_run.cc
  src/cmd_datasets.cc
  src/cmd_validate.cc
  src/cmd_dev_run.cc
  src/datasets_check.cc
  src/dev_run.cc
  src/event_digest.cc
  src/export3d.cc
  src/event_seed.cc
  src/atomic_rename.cc
  src/build_info.cc
  src/cancellation.cc
  src/failure.cc
  src/geant4_errors.cc
  src/hits_writer.cc
  src/run_outputs.cc
  src/run_pipeline.cc
  src/sha3_256.cc
  src/spec_file.cc
  src/stdout_channel.cc
  src/spec_validate.cc
  src/spec_canonical.cc
  src/spec_catalogue.cc
)
target_include_directories(vikshep_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_link_libraries(vikshep_core PUBLIC
  nanoarrow_vendored
  tiny_sha3
  Geant4::G4physicslists
  CLI11::CLI11
  nlohmann_json::nlohmann_json
)

add_executable(vikshep-sim src/main.cc)
target_link_libraries(vikshep-sim PRIVATE vikshep_core)

# Tests: ctest --test-dir build --output-on-failure
enable_testing()
find_package(Catch2 3 REQUIRED)
# The tests use pyarrow, jsonschema and matplotlib, which are in the conda environment: use
# its Python, not another one on the machine. (FindPython3 picked a system Python of the
# same version in a fresh environment, so the interpreter is looked up here first.)
set(VIKSHEP_PYTHON_HINTS "")
if(DEFINED ENV{CONDA_PREFIX})
  list(APPEND VIKSHEP_PYTHON_HINTS "$ENV{CONDA_PREFIX}/bin")
endif()
foreach(_prefix IN LISTS CMAKE_PREFIX_PATH)
  list(APPEND VIKSHEP_PYTHON_HINTS "${_prefix}/bin")
endforeach()
find_program(VIKSHEP_PYTHON NAMES python3 python HINTS ${VIKSHEP_PYTHON_HINTS} NO_DEFAULT_PATH)
if(NOT VIKSHEP_PYTHON)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  set(VIKSHEP_PYTHON "${Python3_EXECUTABLE}" CACHE FILEPATH "Python for the tests" FORCE)
endif()
message(STATUS "Python for the tests: ${VIKSHEP_PYTHON}")
include(Catch)

add_executable(vikshep-tests
  tests/test_spec.cc
  tests/test_tracker_spec.cc
  tests/test_export3d.cc
  tests/test_cli.cc
  tests/test_datasets.cc
  tests/test_reorder_buffer.cc
  tests/test_event_seed.cc
  tests/test_event_digest.cc
  tests/test_vendored.cc
  tests/test_sha3.cc
  tests/test_byte_compare.cc
  tests/test_hits_writer.cc
  tests/test_run_outputs.cc
  tests/test_digests.cc
  tests/test_outputs.cc
  tests/test_plot.cc
  tests/test_atomic_rename.cc
  tests/test_channel.cc
  tests/test_progress.cc
  tests/test_run.cc
  tests/test_g4_fatal.cc
)
target_link_libraries(vikshep-tests PRIVATE vikshep_core Catch2::Catch2WithMain)
target_compile_definitions(vikshep-tests PRIVATE
  VIKSHEP_CHANNEL_PROBE="$<TARGET_FILE:channel-probe>"
  VIKSHEP_PYTHON="${VIKSHEP_PYTHON}"
  VIKSHEP_TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests"
  VIKSHEP_SCRIPTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scripts"
  VIKSHEP_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden"
  VIKSHEP_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/docs/examples"
  VIKSHEP_SIM_EXE="$<TARGET_FILE:vikshep-sim>"
)
add_dependencies(vikshep-tests vikshep-sim)
# Every test has a time limit, so that a stall fails the test with a clear message instead of
# hanging a CI job (a test in a fresh environment once took 1008 s, see STATUS.md). The slowest
# test of a normal run takes about 15 s.
set(VIKSHEP_TEST_TIMEOUT 120)
catch_discover_tests(vikshep-tests PROPERTIES TIMEOUT ${VIKSHEP_TEST_TIMEOUT})

# One Geant4 run per process, so the simulation test has its own executable.
add_executable(vikshep-sim-tests tests/test_dev_run.cc)
target_link_libraries(vikshep-sim-tests PRIVATE vikshep_core Catch2::Catch2WithMain)
target_compile_definitions(vikshep-sim-tests PRIVATE
  VIKSHEP_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/docs/examples"
)
catch_discover_tests(vikshep-sim-tests PROPERTIES TIMEOUT ${VIKSHEP_TEST_TIMEOUT})

# Test-only program for the stdout channel and the signal handlers.
add_executable(channel-probe tests/channel_probe.cc)
target_link_libraries(channel-probe PRIVATE vikshep_core)

# A test-only gun that draws a random number: independent of the thread count.
add_executable(random-gun-probe tests/random_gun_probe.cc)
target_link_libraries(random-gun-probe PRIVATE vikshep_core)
target_compile_definitions(random-gun-probe PRIVATE
  VIKSHEP_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/docs/examples"
)
add_test(NAME random_draws_independent_of_threads
  COMMAND ${VIKSHEP_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_random_gun.py
          $<TARGET_FILE:random-gun-probe>
)
set_tests_properties(random_draws_independent_of_threads PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT ${VIKSHEP_TEST_TIMEOUT})

# The golden hits files, read with pyarrow and hashlib (independent of the C++ writer).
add_test(NAME golden_hits_files_valid
  COMMAND ${VIKSHEP_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_golden.py
          ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden
)
set_tests_properties(golden_hits_files_valid PROPERTIES TIMEOUT ${VIKSHEP_TEST_TIMEOUT})

# Cancellation of `run` by SIGTERM and SIGINT, with the cancel latency (skipped, exit 77,
# when the Geant4 data sets are missing).
add_test(NAME run_cancellation_by_signal
  COMMAND ${VIKSHEP_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_cancel.py
          $<TARGET_FILE:vikshep-sim>
          ${CMAKE_CURRENT_SOURCE_DIR}/tests/long_run_spec.json
)
set_tests_properties(run_cancellation_by_signal PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT ${VIKSHEP_TEST_TIMEOUT})

# The stdout of `run --progress jsonl`: purity, shape, order and cadence, for 1 and 4 threads.
foreach(_threads 1 4)
  add_test(NAME run_stdout_stream_${_threads}_threads
    COMMAND ${VIKSHEP_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_run_stream.py
            $<TARGET_FILE:vikshep-sim>
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/stream_spec.json ${_threads}
  )
  set_tests_properties(run_stdout_stream_${_threads}_threads PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT ${VIKSHEP_TEST_TIMEOUT})
endforeach()

add_test(NAME fast_math_is_refused_and_absent
  COMMAND ${VIKSHEP_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_fast_math.py
          ${CMAKE_COMMAND} ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR}
)
set_tests_properties(fast_math_is_refused_and_absent PROPERTIES TIMEOUT ${VIKSHEP_TEST_TIMEOUT})

add_test(NAME schema_matches_validator
  COMMAND ${VIKSHEP_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_schema.py
          $<TARGET_FILE:vikshep-sim>
          ${CMAKE_CURRENT_SOURCE_DIR}/docs/spec.schema.json
          ${CMAKE_CURRENT_SOURCE_DIR}/docs/examples
)
set_tests_properties(schema_matches_validator PROPERTIES TIMEOUT ${VIKSHEP_TEST_TIMEOUT})

# Spec 1.0 keeps its meaning in a build that also accepts 1.1: spec_oid, validity and (for the
# builds with a recorded value) the OID of hits.arrow of the three 1.0 examples.
add_test(NAME spec_1_0_examples_unchanged
  COMMAND ${VIKSHEP_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_spec10_golden.py
          $<TARGET_FILE:vikshep-sim>
          ${CMAKE_CURRENT_SOURCE_DIR}/docs/examples
          ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden
)
set_tests_properties(spec_1_0_examples_unchanged PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT ${VIKSHEP_TEST_TIMEOUT})

# tracker_barrel and the uniform field: determinism, zero field = no field, and the physics
# (straight line at B = 0, radius of curvature pT = 0.3 B R), with the numbers printed.
add_test(NAME tracker_determinism_and_zero_field
  COMMAND ${VIKSHEP_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_tracker_determinism.py
          $<TARGET_FILE:vikshep-sim>
          ${CMAKE_CURRENT_SOURCE_DIR}/docs/examples
)
set_tests_properties(tracker_determinism_and_zero_field PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT ${VIKSHEP_TEST_TIMEOUT})

add_test(NAME tracker_physics_straight_line_and_curvature
  COMMAND ${VIKSHEP_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_tracker_physics.py
          $<TARGET_FILE:vikshep-sim>
)
set_tests_properties(tracker_physics_straight_line_and_curvature PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT ${VIKSHEP_TEST_TIMEOUT})

# The 3-D export (docs/EXPORT3D.md): the files parse and follow the documented layout, hits.arrow
# and its OID are the same with the export on and off (1 and 4 threads), the exported files are
# thread-count independent, and the trajectories agree with the hits.
add_test(NAME export3d_files_and_oid_unchanged
  COMMAND ${VIKSHEP_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_export3d.py
          $<TARGET_FILE:vikshep-sim>
          ${CMAKE_CURRENT_SOURCE_DIR}/docs/examples
          ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden
          ${CMAKE_CURRENT_SOURCE_DIR}/tests/trajectories-2.0.schema.json
)
set_tests_properties(export3d_files_and_oid_unchanged PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT ${VIKSHEP_TEST_TIMEOUT})

# Format 2 of trajectories.json (docs/EXPORT3D.md section 7): physics numbers from the teaching
# examples (time, photon speed, energy balance, process names, field radius).
add_test(NAME export3d_physics_numbers
  COMMAND ${VIKSHEP_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_export_physics.py
          $<TARGET_FILE:vikshep-sim>
          ${CMAKE_CURRENT_SOURCE_DIR}/docs/examples
)
set_tests_properties(export3d_physics_numbers PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT ${VIKSHEP_TEST_TIMEOUT})

# Both versions of trajectories.json: schemas, the format 1 file of release v0.1.0, the reader rule.
# Needs no simulation.
add_test(NAME export3d_trajectory_formats
  COMMAND ${VIKSHEP_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_trajectory_formats.py
          ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
set_tests_properties(export3d_trajectory_formats PROPERTIES TIMEOUT ${VIKSHEP_TEST_TIMEOUT})

# geometry.glb of both examples passes the Khronos glTF Validator (pinned in tests/gltf-validator)
# with no error and no warning. Skipped (77) without node or `npm ci` there; a failure under
# VIKSHEP_REQUIRE_DATA.
add_test(NAME export3d_glb_khronos_validator
  COMMAND ${VIKSHEP_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_glb_validator.py
          $<TARGET_FILE:vikshep-sim>
          ${CMAKE_CURRENT_SOURCE_DIR}/docs/examples
          ${CMAKE_CURRENT_SOURCE_DIR}
)
set_tests_properties(export3d_glb_khronos_validator PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT ${VIKSHEP_TEST_TIMEOUT})
