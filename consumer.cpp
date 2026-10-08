#include <args.hxx>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <linux/limits.h>
#include <string>

// Replaces the first %d with `module` and the second %d with `file` in a copy
// of `format`. If only one %d exists (or none), it is filled with `module`
// (0 when modules==1 so single-%d behaviour is preserved).
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

    args::ArgumentParser parser(
        "consumer: components of the IOFilePatternBenchmark suite that produces files",
        "Developed by Marco Edoardo Santimaria\nmarcoedoardo.santimaria@unito.it - 2025 ");
    parser.LongSeparator(" ");
    parser.LongPrefix("--");
    parser.ShortPrefix("-");

    args::Group arguments(parser, "Arguments");
    args::HelpFlag help(arguments, "", "Display this help menu", {'h', "help"});

    args::ValueFlag<int> window_size_args(arguments, "Kilobytes", "Window size for IO operation",
                                          {'w', "window"});

    args::ValueFlag<std::string> input_file_format_arg(
        arguments, "Filename",
        "Input file name (optionally \"file format\" if flag -c > 1). Note: use %d to specify "
        "where index of file should be placed. Default to " +
            input_file_format,
        {'o', "output"});

    args::ValueFlag<int> file_count_arg(arguments, "Count", "Number of output files to produce",
                                        {'c', "count"});

    args::ValueFlag<std::int64_t> file_size_arg(arguments, "Kilobytes",
                                                "Size of each produced file", {'s', "size"});

    args::ValueFlag<int> modules_arg(arguments, "Modules",
                                     "Number of module sub-streams (fanin). Enabled only if > 1",
                                     {'m', "modules"});

    try {
        parser.ParseCLI(argc, argv);
    } catch (args::Help &e) {
        std::cout << "ERROR: " << e.what() << std::endl << parser << std::endl;
        exit(EXIT_FAILURE);
    }

    if (window_size_args) {
        window_size = args::get(window_size_args);
    }

    if (input_file_format_arg) {
        input_file_format = args::get(input_file_format_arg);
    }

    if (file_count_arg) {
        file_count = args::get(file_count_arg);
    }

    if (file_size_arg) {
        file_size = args::get(file_size_arg);
    }

    if (modules_arg) {
        modules = args::get(modules_arg);
        if (modules < 1) {
            modules = 1;
        }
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
