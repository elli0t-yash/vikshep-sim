# Standalone build of the seeding probe; not part of the vikshep-sim build.
#   cmake -S tools/seedprobe -B build-seedprobe -G Ninja \
#         -DCMAKE_PREFIX_PATH="$CONDA_PREFIX" -DCMAKE_FIND_FRAMEWORK=LAST
cmake_minimum_required(VERSION 3.20)
project(seedprobe CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Geant4 REQUIRED)
add_executable(seedprobe seedprobe.cc)
target_link_libraries(seedprobe PRIVATE Geant4::G4physicslists)
