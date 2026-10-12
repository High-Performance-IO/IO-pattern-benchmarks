#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <linux/limits.h>

#include "argparse.h"

int main(int argc, char **argv) {

    std::chrono::steady_clock::time_point absolute_begin = std::chrono::steady_clock::now();

    std::string output_file_format = "file_%d.dat";
    int window_size                = 1024;
    std::int64_t file_size         = 1024LL * 1024 * 1024; // 1GB file
    int file_count                 = 1;
    std::string pattern            = "streaming";

    cli::Parser parser(
        "producer: components of the IOFilePatternBenchmark suite that produces files");
    parser.add('w', "window", std::to_string(window_size), "Window size for IO operation [KB]");
    parser.add('o', "output", output_file_format,
               "Output file name, optionally \"file format\" if flag -c > 1. Use %d for the file "
               "index");
    parser.add('c', "count", std::to_string(file_count), "Number of output files to produce");
    parser.add('s', "size", std::to_string(file_size), "Size of each produced file [KB]");
    parser.add('\0', "pattern", pattern, "Write pattern: streaming or backward-seeks");

    if (!parser.parse(argc, argv)) {
        std::cout << (parser.help_requested() ? parser.help()
                                              : "ERROR: invalid arguments\n" + parser.help())
                  << std::endl;
        return EXIT_FAILURE;
    }

    window_size        = parser["window"].get<int>();
    output_file_format = parser["output"].get<std::string>();
    file_count         = parser["count"].get<int>();
    file_size          = parser["size"].get<std::int64_t>();
    pattern            = parser["pattern"].get<std::string>();

    if (pattern != "streaming" && pattern != "backward-seeks") {
        std::cout << "ERROR: Unknown pattern '" << pattern
                  << "'. Expected streaming or backward-seeks" << std::endl;
        exit(EXIT_FAILURE);
    }

    std::cout << "*========================================*" << std::endl
              << "| Test configuration:" << std::endl
              << "| Output Format: \t" << output_file_format << std::endl
              << "| File count: \t\t" << file_count << std::endl
              << "| File size: \t\t" << file_size << std::endl
              << "| Window size: \t\t" << window_size << std::endl
              << "| Pattern: \t\t" << pattern << std::endl
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

    if (pattern == "backward-seeks" &&
        static_cast<std::uintmax_t>(file_size) >
            static_cast<std::uintmax_t>(std::numeric_limits<std::streamoff>::max())) {
        std::cout << "ERROR: File size exceeds the supported seek range" << std::endl;
        exit(EXIT_FAILURE);
    }

    auto buffer = new char[window_size];
    auto input  = fopen("/dev/urandom", "r");
    fread(buffer, window_size, 1, input);
    fclose(input);

    for (auto i = 0; i < file_count; i++) {
        char output_file_name[PATH_MAX]{0};

        sprintf(output_file_name, output_file_format.c_str(), i);
        std::cout << "Writing to file: " << output_file_name;

        std::chrono::steady_clock::time_point test_start = std::chrono::steady_clock::now();
        std::ofstream output_file(output_file_name, std::ios::binary);

        if (pattern == "streaming") {
            std::int64_t write_operations = file_size / window_size;
            std::int64_t extra_write_size = file_size % window_size;
            for (std::int64_t operation_id = 0; operation_id < write_operations; operation_id++) {
                output_file.write(buffer, window_size);
            }

            if (extra_write_size) {
                output_file.write(buffer, extra_write_size);
            }
        } else {
            auto write_offset = static_cast<std::streamoff>(file_size);
            output_file.seekp(write_offset);
            while (write_offset > 0) {
                int write_size =
                    static_cast<int>(std::min<std::streamoff>(window_size, write_offset));
                write_offset -= write_size;
                output_file.seekp(write_offset);
                output_file.write(buffer, write_size);
            }
        }

        output_file.close();
        std::chrono::steady_clock::time_point test_end = std::chrono::steady_clock::now();
        std::cout
            << " - took: "
            << std::chrono::duration_cast<std::chrono::microseconds>(test_end - test_start).count()
            << "[µs]" << std::endl;
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
