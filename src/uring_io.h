#pragma once

#include <cstdint>
#include <liburing.h>

namespace uring {

// Reads or writes `size` bytes at `offset` with io_uring, one request at a time, and returns
// the bytes transferred. The result is smaller than `size` only at end of file or on an error.
inline std::int64_t transfer(io_uring &ring, bool is_write, int fd, char *buffer,
                             std::int64_t size, std::int64_t offset) {
    std::int64_t done = 0;
    while (done < size) {
        io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        if (is_write) {
            io_uring_prep_write(sqe, fd, buffer + done, size - done, offset + done);
        } else {
            io_uring_prep_read(sqe, fd, buffer + done, size - done, offset + done);
        }
        io_uring_submit(&ring);

        io_uring_cqe *cqe = nullptr;
        if (io_uring_wait_cqe(&ring, &cqe) < 0) {
            break;
        }
        int result = cqe->res;
        io_uring_cqe_seen(&ring, cqe);
        if (result <= 0) {
            break; // error, or end of file on a read
        }
        done += result;
    }
    return done;
}

} // namespace uring
