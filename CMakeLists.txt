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

add_executable(vikshep-sim
  src/main.cc
  src/cmd_info.cc
  src/cmd_run.cc
  src/cmd_datasets.cc
)
target_include_directories(vikshep-sim PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_link_libraries(vikshep-sim PRIVATE
  Geant4::G4physicslists
  CLI11::CLI11
  nlohmann_json::nlohmann_json
)
