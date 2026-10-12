cmake_minimum_required(VERSION 3.10.0)
project(IOFilePatternBenchmarks
        DESCRIPTION "Benchmarks to evaulate differnt IO access patterns"
        LANGUAGES CXX
        VERSION 0.0.1
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall -pedantic -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

#####################################
# Targets
#####################################
add_executable(producer src/producer.cpp)
add_executable(consumer src/consumer.cpp)
add_executable(prodcons src/prodcons.cpp)



