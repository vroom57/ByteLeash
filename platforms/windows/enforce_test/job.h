// Job object network rate control for the enforcement test: puts this
// process in a job and limits the job's outgoing bandwidth
// (JobObjectNetRateControlInformation, Windows 10 1607+). Windows' own packet
// scheduler does the pacing; downloads aren't limited. The limit ends with
// the process.

#pragma once

#include <cstdint>

namespace byteleash::enforce_test {

class JobThrottle {
public:
    JobThrottle() = default;
    ~JobThrottle();
    JobThrottle(const JobThrottle&) = delete;
    JobThrottle& operator=(const JobThrottle&) = delete;

    // Each returns 0 or a Win32 error code.
    // Creates the job and assigns this process to it.
    unsigned long open();
    unsigned long setLimit(std::uint64_t bytes_per_second);
    unsigned long clearLimit();

private:
    void* job_ = nullptr;
};

}  // namespace byteleash::enforce_test
