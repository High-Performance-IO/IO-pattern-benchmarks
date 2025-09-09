#include <args.hxx>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <linux/limits.h>

int main(int argc, char **argv) {

    std::chrono::steady_clock::time_point absolute_begin = std::chrono::steady_clock::now();

    std::string output_file_format = "file_%d.dat";
    int window_size                = 1024;
    int file_size                  = 1024 * 1024 * 1024; // 1GB file
    int file_count                 = 1;

    args::ArgumentParser parser(
        "producer: components of the IOFilePatternBenchmark suite that produces files",
        "Developed by Marco Edoardo Santimaria\nmarcoedoardo.santimaria@unito.it - 2025 ");
    parser.LongSeparator(" ");
    parser.LongPrefix("--");
    parser.ShortPrefix("-");

    args::Group arguments(parser, "Arguments");
    args::HelpFlag help(arguments, "", "Display this help menu", {'h', "help"});

    args::ValueFlag<int> window_size_args(arguments, "Kilobytes", "Window size for IO operation",
                                          {'w', "window"});

    args::ValueFlag<std::string> output_file_format_arg(
        arguments, "Filename",
        "Output file name (optionally \"file format\" if flag -c > 1). Note: use %d to specify "
        "where index of file should be placed. Default to " +
            output_file_format,
        {'o', "output"});

    args::ValueFlag<int> file_count_arg(arguments, "Count", "Number of output files to produce",
                                        {'c', "count"});

    args::ValueFlag<int> file_size_arg(arguments, "Kilobytes", "Size of each produced file",
                                       {'s', "size"});

    try {
        parser.ParseCLI(argc, argv);
    } catch (args::Help &e) {
        std::cout << "ERROR: " << e.what() << std::endl << parser << std::endl;
        exit(EXIT_FAILURE);
    }

    if (window_size_args) {
        window_size = args::get(window_size_args);
    }

    if (output_file_format_arg) {
        output_file_format = args::get(output_file_format_arg);
    }

    if (file_count_arg) {
        file_count = args::get(file_count_arg);
    }

    if (file_size_arg) {
        file_size = args::get(file_size_arg);
    }

    std::cout << "*========================================*" << std::endl
              << "| Test configuration:" << std::endl
              << "| Output Format: \t" << output_file_format << std::endl
              << "| File count: \t\t" << file_count << std::endl
              << "| File size: \t\t" << file_size << std::endl
              << "| Window size: \t\t" << window_size << std::endl
              << "*========================================*" << std::endl
              << std::endl;

    if (file_size < window_size) {
        std::cout << "ERROR: File size must be greater than or equal to window size" << std::endl;
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

        int write_operations = file_size / window_size;
        int extra_write_size = file_size % window_size;
        for (int operation_id = 0; operation_id < write_operations; operation_id++) {
            output_file.write(buffer, window_size);
        }

        if (extra_write_size) {
            output_file.write(buffer, extra_write_size);
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