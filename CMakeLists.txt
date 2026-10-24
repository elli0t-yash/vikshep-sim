cmake_minimum_required(VERSION 3.20)
project(vikshep-sim VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
  src/event_seed.cc
  src/hits_writer.cc
  src/sha3_256.cc
  src/spec_file.cc
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
find_package(Python3 COMPONENTS Interpreter REQUIRED)
include(Catch)

add_executable(vikshep-tests
  tests/test_spec.cc
  tests/test_cli.cc
  tests/test_datasets.cc
  tests/test_reorder_buffer.cc
  tests/test_event_seed.cc
  tests/test_event_digest.cc
  tests/test_vendored.cc
  tests/test_sha3.cc
  tests/test_hits_writer.cc
  tests/test_digests.cc
)
target_link_libraries(vikshep-tests PRIVATE vikshep_core Catch2::Catch2WithMain)
target_compile_definitions(vikshep-tests PRIVATE
  VIKSHEP_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden"
  VIKSHEP_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/docs/examples"
  VIKSHEP_SIM_EXE="$<TARGET_FILE:vikshep-sim>"
)
add_dependencies(vikshep-tests vikshep-sim)
catch_discover_tests(vikshep-tests)

# One Geant4 run per process, so the simulation test has its own executable.
add_executable(vikshep-sim-tests tests/test_dev_run.cc)
target_link_libraries(vikshep-sim-tests PRIVATE vikshep_core Catch2::Catch2WithMain)
target_compile_definitions(vikshep-sim-tests PRIVATE
  VIKSHEP_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/docs/examples"
)
catch_discover_tests(vikshep-sim-tests)

# A test-only gun that draws a random number: independent of the thread count.
add_executable(random-gun-probe tests/random_gun_probe.cc)
target_link_libraries(random-gun-probe PRIVATE vikshep_core)
target_compile_definitions(random-gun-probe PRIVATE
  VIKSHEP_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/docs/examples"
)
add_test(NAME random_draws_independent_of_threads
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_random_gun.py
          $<TARGET_FILE:random-gun-probe>
)
set_tests_properties(random_draws_independent_of_threads PROPERTIES SKIP_RETURN_CODE 77)

# The golden hits files, read with pyarrow and hashlib (independent of the C++ writer).
add_test(NAME golden_hits_files_valid
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_golden.py
          ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden
)

add_test(NAME schema_matches_validator
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_schema.py
          $<TARGET_FILE:vikshep-sim>
          ${CMAKE_CURRENT_SOURCE_DIR}/docs/spec.schema.json
          ${CMAKE_CURRENT_SOURCE_DIR}/docs/examples
)
