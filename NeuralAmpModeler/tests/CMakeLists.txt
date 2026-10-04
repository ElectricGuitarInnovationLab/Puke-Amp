cmake_minimum_required(VERSION 3.14)
project(NAMAssetLibraryTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)
enable_testing()

get_filename_component(REPOSITORY_ROOT "../.." ABSOLUTE)
add_executable(asset_library_test asset_library_test.cpp)
target_link_libraries(asset_library_test PRIVATE Threads::Threads)
add_test(NAME asset_library COMMAND asset_library_test)

file(GLOB NAM_SOURCES "${REPOSITORY_ROOT}/NeuralAmpModelerCore/NAM/*.cpp"
                      "${REPOSITORY_ROOT}/NeuralAmpModelerCore/NAM/*/*.cpp")
add_executable(asset_validation_test asset_validation_test.cpp ${NAM_SOURCES}
               "${REPOSITORY_ROOT}/AudioDSPTools/dsp/wav.cpp")
target_include_directories(asset_validation_test SYSTEM PRIVATE
                           "${REPOSITORY_ROOT}/eigen"
                           "${REPOSITORY_ROOT}/NeuralAmpModelerCore/Dependencies/nlohmann")
target_link_libraries(asset_validation_test PRIVATE Threads::Threads)
file(GLOB AMP_FIXTURES "${REPOSITORY_ROOT}/Models/Amp/*.nam")
file(GLOB PEDAL_FIXTURES "${REPOSITORY_ROOT}/Models/FX/*.nam")
file(GLOB IR_FIXTURES "${REPOSITORY_ROOT}/Models/IR/*.wav")
list(GET AMP_FIXTURES 0 AMP_FIXTURE)
list(GET PEDAL_FIXTURES 0 PEDAL_FIXTURE)
list(GET IR_FIXTURES 0 IR_FIXTURE)
add_test(NAME asset_validation COMMAND asset_validation_test
         "${AMP_FIXTURE}" "${PEDAL_FIXTURE}" "${IR_FIXTURE}")

# Assertions are the test oracle, including in release builds.
foreach(target asset_library_test asset_validation_test)
  if(MSVC)
    target_compile_options(${target} PRIVATE /UNDEBUG)
    target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  else()
    target_compile_options(${target} PRIVATE -UNDEBUG)
  endif()
endforeach()
