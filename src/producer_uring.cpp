// producer, with the writes done through io_uring (liburing) instead of std::ofstream.
// Same options and same output as producer.cpp.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fcntl.h>
#include <iostream>
#include <linux/limits.h>
#include <unistd.h>

#include "argparse.h"
#include "djb2.h"
#include "uring_io.h"

int main(int argc, char **argv) {

    std::chrono::steady_clock::time_point absolute_begin = std::chrono::steady_clock::now();

    std::string output_file_format = "file_%d.dat";
    int window_size                = 1024;
    std::int64_t file_size         = 1024LL * 1024 * 1024; // 1GB file
    int file_count                 = 1;
    std::string pattern            = "streaming";
    int compute                    = 0;

    cli::Parser parser(
        "producer_uring: producer of the IOFilePatternBenchmark suite that writes with io_uring");
    parser.add('w', "window", std::to_string(window_size), "Window size for IO operation [KB]");
    parser.add('o', "output", output_file_format,
               "Output file name, optionally \"file format\" if flag -c > 1. Use %d for the file "
               "index");
    parser.add('c', "count", std::to_string(file_count), "Number of output files to produce");
    parser.add('s', "size", std::to_string(file_size), "Size of each produced file [KB]");
    parser.add('\0', "pattern", pattern, "Write pattern: streaming or backward-seeks");
    parser.add('\0', "compute", std::to_string(compute),
               "Times to compute djb2 hash on each written chunk (0 = disabled)");

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
    compute            = parser["compute"].get<int>();

    if (compute < 0) {
        std::cout << "ERROR: --compute must be >= 0" << std::endl;
        exit(EXIT_FAILURE);
    }

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
              << "| Compute: \t\t" << compute << std::endl
              << "| Engine: \t\tio_uring" << std::endl
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
    auto input  = fopen("/dev/urandom", "r");
    fread(buffer, window_size, 1, input);
    fclose(input);

    io_uring ring{};
    if (io_uring_queue_init(8, &ring, 0) < 0) {
        std::cout << "ERROR: cannot create the io_uring ring" << std::endl;
        exit(EXIT_FAILURE);
    }

    std::int64_t bytes_written = 0;
    for (auto i = 0; i < file_count; i++) {
        char output_file_name[PATH_MAX]{0};

        sprintf(output_file_name, output_file_format.c_str(), i);
        std::cout << "Writing to file: " << output_file_name;

        std::chrono::steady_clock::time_point test_start = std::chrono::steady_clock::now();
        int fd = open(output_file_name, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd < 0) {
            std::cout << std::endl << "ERROR: cannot open " << output_file_name << std::endl;
            exit(EXIT_FAILURE);
        }

        // Every io_uring write carries its offset: streaming goes from offset zero to the end
        // of the file, backward-seeks from the end of the file to offset zero.
        std::int64_t file_bytes = 0;
        while (file_bytes < file_size) {
            std::int64_t write_size = std::min<std::int64_t>(window_size, file_size - file_bytes);
            std::int64_t offset     =
                pattern == "streaming" ? file_bytes : file_size - file_bytes - write_size;
            djb2::hash_chunks(buffer, write_size, compute);
            if (uring::transfer(ring, true, fd, buffer, write_size, offset) != write_size) {
                std::cout << std::endl
                          << "ERROR: write to " << output_file_name << " failed" << std::endl;
                exit(EXIT_FAILURE);
            }
            file_bytes += write_size;
        }

        // As in producer.cpp: flush the file to disk and evict its pages, so that the
        // consumer's read touches storage instead of the warm page cache.
        fsync(fd);
        posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
        close(fd);

        bytes_written += file_size;

        std::chrono::steady_clock::time_point test_end = std::chrono::steady_clock::now();
        std::cout
            << " - took: "
            << std::chrono::duration_cast<std::chrono::microseconds>(test_end - test_start).count()
            << "[µs]" << std::endl;
    }

    io_uring_queue_exit(&ring);
    delete[] buffer;

    std::cout << "IO bytes: read=0 written=" << bytes_written << std::endl;

    std::chrono::steady_clock::time_point absolute_end = std::chrono::steady_clock::now();
    std::cout << "Execution elapsed time: "
              << std::chrono::duration_cast<std::chrono::microseconds>(absolute_end -
                                                                       absolute_begin)
                     .count()
              << "[µs]" << std::endl;
    return 0;
}
