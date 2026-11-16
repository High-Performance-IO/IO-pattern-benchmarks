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




#####################################
# io_uring targets (built only when liburing is installed)
#####################################
find_package(PkgConfig)
if (PkgConfig_FOUND)
    pkg_check_modules(LIBURING IMPORTED_TARGET liburing)
endif ()
if (LIBURING_FOUND)
    add_executable(producer_uring src/producer_uring.cpp)
    add_executable(consumer_uring src/consumer_uring.cpp)
    target_link_libraries(producer_uring PRIVATE PkgConfig::LIBURING)
    target_link_libraries(consumer_uring PRIVATE PkgConfig::LIBURING)
else ()
    message(STATUS "liburing not found: producer_uring and consumer_uring are not built")
endif ()
