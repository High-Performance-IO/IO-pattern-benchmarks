#pragma once

#include <cstddef>
#include <cstdint>

namespace djb2 {

inline std::uint64_t hash(const char *data, std::size_t len, std::uint64_t seed = 5381) {
    for (std::size_t i = 0; i < len; ++i) {
        seed = seed * 33 + static_cast<unsigned char>(data[i]);
    }
    return seed;
}

inline void hash_chunks(const char *data, const std::size_t len, const int count) {
    if (count <= 0) {
        return;
    }
    std::uint64_t h = 5381;
    for (int k = 0; k < count; ++k) {
        h = hash(data, len, h);
    }

    // these tree lines avoid compilers to optimize out this function, thus maintaining the compute
    // aspect of the benchmark
    static volatile std::uint64_t sink = 0;
    sink                               = h;
    (void) sink;
}

} // namespace djb2