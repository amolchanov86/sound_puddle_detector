cmake_minimum_required (VERSION 3.5)
project (mic_read_thread)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_BUILD_TYPE Debug)

find_package(ALSA REQUIRED) 
find_package(Threads REQUIRED)
#find_package(Boost COMPONENTS system REQUIRED)

#add_subdirectory(libs/yaml_cpp)
#include_directories(libs/yaml_cpp/include)

add_executable(example_calsa_mic_recording examples/example_calsa_mic_recording.c)
target_link_libraries(example_calsa_mic_recording ${Boost_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} ${ALSA_LIBRARIES})

add_executable(example_calsa2_mic_recording examples/example_calsa2_mic_recording.c)
target_link_libraries(example_calsa2_mic_recording ${ALSA_LIBRARIES})

add_executable(example_sine examples/example_sine.cpp)
target_link_libraries(example_sine ${ALSA_LIBRARIES})

add_executable(endianess examples/endianess.cpp)

add_executable(${PROJECT_NAME} micread_main.cpp micread_thread.cpp)
target_link_libraries(${PROJECT_NAME} ${Boost_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} ${ALSA_LIBRARIES})
