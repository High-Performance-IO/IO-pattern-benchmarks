cmake_minimum_required(VERSION 4.0)
project(IOFilePatternBenchmarks
        DESCRIPTION "Benchmarks to evaulate differnt IO access patterns"
        LANGUAGES CXX
        VERSION 0.0.1
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall -pedantic -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

#####################################
# External projects
#####################################
include(FetchContent)
FetchContent_Declare(
        args
        GIT_REPOSITORY https://github.com/Taywee/args.git
        GIT_TAG 6.4.7
)
set(ARGS_BUILD_EXAMPLE OFF CACHE INTERNAL "")
set(ARGS_BUILD_UNITTESTS OFF CACHE INTERNAL "")


FetchContent_MakeAvailable(args)


add_executable(producer producer.cpp)
add_executable(consumer consumer.cpp)

target_include_directories(producer PRIVATE ${args_SOURCE_DIR} )
target_include_directories(consumer PRIVATE ${args_SOURCE_DIR} )



