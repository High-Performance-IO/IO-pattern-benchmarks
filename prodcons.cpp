#include <args.hxx>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <linux/limits.h>
#include <limits>

int main(int argc, char **argv) {

    std::chrono::steady_clock::time_point absolute_begin = std::chrono::steady_clock::now();

    std::string input_file_format  = "in_%d.dat";
    std::string output_file_format = "out_%d.dat";
    int window_size                = 1024;
    std::int64_t file_size         = 1024LL * 1024 * 1024; // 1GB file
    int file_count                 = 1;
    std::string pattern            = "streaming";

    args::ArgumentParser parser(
        "prodcons: IOFilePatternBenchmark pipeline stage that consumes files and produces "
        "output files",
        "Developed by Marco Edoardo Santimaria\nmarcoedoardo.santimaria@unito.it - 2025 ");
    parser.LongSeparator(" ");
    parser.LongPrefix("--");
    parser.ShortPrefix("-");

    args::Group arguments(parser, "Arguments");
    args::HelpFlag help(arguments, "", "Display this help menu", {'h', "help"});

    args::ValueFlag<int> window_size_args(arguments, "Kilobytes", "Window size for IO operation",
                                          {'w', "window"});

    args::ValueFlag<int> file_count_arg(arguments, "Count", "Number of files to process",
                                        {'c', "count"});

    args::ValueFlag<std::int64_t> file_size_arg(arguments, "Kilobytes",
                                                "Size of each input file", {'s', "size"});

    args::ValueFlag<std::string> input_file_format_arg(
        arguments, "Filename",
        "Input file name (optionally \"file format\" if flag -c > 1). Note: use %d to specify "
        "where index of file should be placed. Default to " +
            input_file_format,
        {'i', "input"});

    args::ValueFlag<std::string> output_file_format_arg(
        arguments, "Filename",
        "Output file name (optionally \"file format\" if flag -c > 1). Note: use %d to specify "
        "where index of file should be placed. Default to " +
            output_file_format,
        {'o', "output"});

    args::ValueFlag<std::string> pattern_arg(
        arguments, "Pattern", "Write pattern: streaming or backward-seeks", {"pattern"});

    try {
        parser.ParseCLI(argc, argv);
    } catch (args::Help &e) {
        std::cout << "ERROR: " << e.what() << std::endl << parser << std::endl;
        exit(EXIT_FAILURE);
    }

    if (window_size_args) {
        window_size = args::get(window_size_args);
    }

    if (file_count_arg) {
        file_count = args::get(file_count_arg);
    }

    if (file_size_arg) {
        file_size = args::get(file_size_arg);
    }

    if (input_file_format_arg) {
        input_file_format = args::get(input_file_format_arg);
    }

    if (output_file_format_arg) {
        output_file_format = args::get(output_file_format_arg);
    }

    if (pattern_arg) {
        pattern = args::get(pattern_arg);
    }

    if (pattern != "streaming" && pattern != "backward-seeks") {
        std::cout << "ERROR: Unknown pattern '" << pattern
                  << "'. Expected streaming or backward-seeks" << std::endl;
        exit(EXIT_FAILURE);
    }

    std::cout << "*========================================*" << std::endl
              << "| Test configuration:" << std::endl
              << "| Input Format: \t" << input_file_format << std::endl
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

    if (pattern == "backward-seeks" && static_cast<std::uintmax_t>(file_size) >
                                           static_cast<std::uintmax_t>(
                                               std::numeric_limits<std::streamoff>::max())) {
        std::cout << "ERROR: File size exceeds the supported seek range" << std::endl;
        exit(EXIT_FAILURE);
    }

    auto buffer = new char[window_size];

    for (auto i = 0; i < file_count; i++) {
        char input_file_name[PATH_MAX]{0};
        char output_file_name[PATH_MAX]{0};

        sprintf(input_file_name, input_file_format.c_str(), i);
        sprintf(output_file_name, output_file_format.c_str(), i);
        std::cout << "Transforming file: " << input_file_name << " -> " << output_file_name;

        std::chrono::steady_clock::time_point test_start = std::chrono::steady_clock::now();
        std::ifstream input_file(input_file_name, std::ios::binary);
        std::ofstream output_file(output_file_name, std::ios::binary);

        std::int64_t read_operations = file_size / window_size;
        std::int64_t extra_read_size = file_size % window_size;

        if (pattern == "streaming") {
            for (std::int64_t operation_id = 0; operation_id < read_operations; operation_id++) {
                input_file.read(buffer, window_size);
                output_file.write(buffer, window_size);
            }
            if (extra_read_size) {
                input_file.read(buffer, extra_read_size);
                output_file.write(buffer, extra_read_size);
            }
        } else {
            std::streamoff write_offset = static_cast<std::streamoff>(file_size);
            output_file.seekp(write_offset);
            for (std::int64_t operation_id = 0; operation_id < read_operations; operation_id++) {
                input_file.read(buffer, window_size);
                write_offset -= window_size;
                output_file.seekp(write_offset);
                output_file.write(buffer, window_size);
            }
            if (extra_read_size) {
                input_file.read(buffer, extra_read_size);
                write_offset -= extra_read_size;
                output_file.seekp(write_offset);
                output_file.write(buffer, extra_read_size);
            }
        }

        output_file.close();
        input_file.close();
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