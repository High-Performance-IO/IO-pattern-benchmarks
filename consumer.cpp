#include "argparse.h"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <linux/limits.h>
#include <string>

static void fill_format(std::string &fmt, std::int64_t module, std::int64_t file) {
    auto first = fmt.find("%d");
    if (first != std::string::npos) {
        fmt.replace(first, 2, std::to_string(module));
    }
    auto second = fmt.find("%d");
    if (second != std::string::npos) {
        fmt.replace(second, 2, std::to_string(file));
    }
}

int main(int argc, char **argv) {

    std::chrono::steady_clock::time_point absolute_begin = std::chrono::steady_clock::now();

    std::string input_file_format = "file_%d.dat";
    int window_size               = 1024;
    std::int64_t file_size        = 1024LL * 1024 * 1024; // 1GB file
    int file_count                = 1;
    int modules                   = 1;

    cli::Parser parser(
        "consumer: components of the IOFilePatternBenchmark suite that consumes files");
    parser.add('w', "window", std::to_string(window_size), "Window size for IO operation [KB]");
    parser.add('o', "output", input_file_format,
               "Input file name, optionally \"file format\" if flag -c > 1. Use %d for the file "
               "index");
    parser.add('c', "count", std::to_string(file_count), "Number of input files to consume");
    parser.add('s', "size", std::to_string(file_size), "Size of each input file [KB]");
    parser.add('m', "modules", std::to_string(modules),
               "Number of module sub-streams (fanin), 1 if unset");

    if (!parser.parse(argc, argv)) {
        std::cout << (parser.help_requested() ? parser.help()
                                              : "ERROR: invalid arguments\n" + parser.help())
                  << std::endl;
        return EXIT_FAILURE;
    }

    window_size       = parser["window"].get<int>();
    input_file_format = parser["output"].get<std::string>();
    file_count        = parser["count"].get<int>();
    file_size         = parser["size"].get<std::int64_t>();
    modules           = parser["modules"].get<int>();
    if (modules < 1) {
        modules = 1;
    }

    std::cout << "*========================================*" << std::endl
              << "| Test configuration:" << std::endl
              << "| Output Format: \t" << input_file_format << std::endl
              << "| File count: \t\t" << file_count << std::endl
              << "| File size: \t\t" << file_size << std::endl
              << "| Window size: \t\t" << window_size << std::endl
              << "| Modules: \t\t" << modules << std::endl
              << "*========================================*" << std::endl
              << std::endl;

    if (file_size <= 0 || window_size <= 0) {
        std::cout << "ERROR: File size and window size must be positive" << std::endl;
        exit(EXIT_FAILURE);
    }

    if (file_size < window_size) {
        std::cout << "ERROR: File size must be greater than or equal to window size" << std::endl;
        exit(EXIT_FAILURE);
    }

    auto buffer = new char[window_size];

    for (int module = 0; module < modules; module++) {
        for (auto i = 0; i < file_count; i++) {
            std::string name = input_file_format;
            fill_format(name, module, i);
            std::cout << "Reading from file: " << name;

            std::chrono::steady_clock::time_point test_start = std::chrono::steady_clock::now();
            std::ifstream output_file(name, std::ios::binary);

            std::int64_t read_operations = file_size / window_size;
            std::int64_t extra_read_size = file_size % window_size;
            for (std::int64_t operation_id = 0; operation_id < read_operations; operation_id++) {
                output_file.read(buffer, window_size);
            }

            if (extra_read_size) {
                output_file.read(buffer, extra_read_size);
            }

            output_file.close();
            std::chrono::steady_clock::time_point test_end = std::chrono::steady_clock::now();
            std::cout << " - took: "
                      << std::chrono::duration_cast<std::chrono::microseconds>(test_end -
                                                                               test_start)
                             .count()
                      << "[µs]" << std::endl;
        }
    }

    delete[] buffer;

    std::chrono::steady_clock::time_point absolute_end = std::chrono::steady_clock::now();
    std::cout << "Execution elapsed time: "
              << std::chrono::duration_cast<std::chrono::microseconds>(absolute_end -
                                                                       absolute_begin)
                     .count()
              << "[µs]" << std::endl;
    return 0;
}
