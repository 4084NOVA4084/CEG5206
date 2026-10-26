#pragma once
#include <cstdio>
#include <string>

#if defined(__unix__) || defined(__APPLE__)
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#define TOPK_HAVE_POSIX_FIFO 1
#endif

// Restricts perf counters to the timed reps (data generation would swamp them):
//   mkfifo ctl ack
//   perf stat --delay=-1 --control=fifo:ctl,ack -e ... --
//       ./bench_topk ... --perf-ctl ctl --perf-ack ack
// Old perf has no --control; run_benchmarks.py then falls back to a two-run diff.

namespace bench {

class PerfControl {
public:
    PerfControl() = default;
    PerfControl(const PerfControl&) = delete;
    PerfControl& operator=(const PerfControl&) = delete;

    ~PerfControl() {
#ifdef TOPK_HAVE_POSIX_FIFO
        if (ctl_fd_ >= 0) close(ctl_fd_);
        if (ack_fd_ >= 0) close(ack_fd_);
#endif
    }

    bool open_fifos(const std::string& ctl_path, const std::string& ack_path) {
        if (ctl_path.empty()) return true;
#ifdef TOPK_HAVE_POSIX_FIFO
        // O_RDWR: opening a FIFO never blocks, even with no perf on the other end
        ctl_fd_ = open(ctl_path.c_str(), O_RDWR | O_CLOEXEC);
        if (ctl_fd_ < 0) {
            std::fprintf(stderr, "perf-ctl: cannot open %s: %s\n", ctl_path.c_str(),
                         std::strerror(errno));
            return false;
        }
        if (!ack_path.empty()) {
            ack_fd_ = open(ack_path.c_str(), O_RDWR | O_CLOEXEC);
            if (ack_fd_ < 0) {
                std::fprintf(stderr, "perf-ack: cannot open %s: %s\n", ack_path.c_str(),
                             std::strerror(errno));
                return false;
            }
        }
        return true;
#else
        (void)ack_path;
        std::fprintf(stderr, "perf-ctl: FIFOs unsupported on this platform\n");
        return false;
#endif
    }

    bool active() const { return ctl_fd_ >= 0; }
    void enable() { send("enable\n"); }
    void disable() { send("disable\n"); }

private:
    void send(const char* command) {
#ifdef TOPK_HAVE_POSIX_FIFO
        if (ctl_fd_ < 0) return;
        const size_t len = std::strlen(command);
        if (write(ctl_fd_, command, len) != static_cast<ssize_t>(len)) {
            std::fprintf(stderr, "perf-ctl: write failed: %s\n", std::strerror(errno));
            return;
        }
        if (ack_fd_ < 0) return;
        // Bounded: a dead perf must not hang the benchmark.
        char buf[16];
        size_t got = 0;
        while (got < 4) {
            pollfd pfd{ack_fd_, POLLIN, 0};
            if (poll(&pfd, 1, 5000) <= 0) {
                std::fprintf(stderr, "perf-ack: timed out waiting for perf\n");
                return;
            }
            const ssize_t r = read(ack_fd_, buf + got, sizeof(buf) - got);
            if (r <= 0) return;
            got += static_cast<size_t>(r);
        }
#else
        (void)command;
#endif
    }

    int ctl_fd_ = -1;
    int ack_fd_ = -1;
};

}  // namespace bench
