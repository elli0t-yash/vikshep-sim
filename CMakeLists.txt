cmake_minimum_required(VERSION 3.20)
project(vikshep-sim VERSION 0.1.0 LANGUAGES CXX)

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

# Everything except main() lives in a library so the tests can link it.
add_library(vikshep_core STATIC
  src/cmd_info.cc
  src/cmd_run.cc
  src/cmd_datasets.cc
  src/cmd_validate.cc
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
)
target_link_libraries(vikshep-tests PRIVATE vikshep_core Catch2::Catch2WithMain)
target_compile_definitions(vikshep-tests PRIVATE
  VIKSHEP_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/docs/examples"
  VIKSHEP_SIM_EXE="$<TARGET_FILE:vikshep-sim>"
)
add_dependencies(vikshep-tests vikshep-sim)
catch_discover_tests(vikshep-tests)

add_test(NAME schema_matches_validator
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_schema.py
          $<TARGET_FILE:vikshep-sim>
          ${CMAKE_CURRENT_SOURCE_DIR}/docs/spec.schema.json
          ${CMAKE_CURRENT_SOURCE_DIR}/docs/examples
)
