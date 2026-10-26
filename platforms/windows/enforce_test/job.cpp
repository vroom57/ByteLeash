#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "job.h"

namespace byteleash::enforce_test {
namespace {

unsigned long setRateControl(HANDLE job, std::uint64_t bytes_per_second, bool enable) {
    JOBOBJECT_NET_RATE_CONTROL_INFORMATION info{};
    info.MaxBandwidth = bytes_per_second;
    info.ControlFlags =
        enable ? static_cast<JOB_OBJECT_NET_RATE_CONTROL_FLAGS>(
                     JOB_OBJECT_NET_RATE_CONTROL_ENABLE |
                     JOB_OBJECT_NET_RATE_CONTROL_MAX_BANDWIDTH)
               : static_cast<JOB_OBJECT_NET_RATE_CONTROL_FLAGS>(0);
    if (!SetInformationJobObject(job, JobObjectNetRateControlInformation, &info,
                                 sizeof(info))) {
        return GetLastError();
    }
    return 0;
}

}  // namespace

JobThrottle::~JobThrottle() {
    if (job_ != nullptr) {
        CloseHandle(job_);
    }
}

unsigned long JobThrottle::open() {
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job == nullptr) {
        return GetLastError();
    }
    if (!AssignProcessToJobObject(job, GetCurrentProcess())) {
        const unsigned long err = GetLastError();
        CloseHandle(job);
        return err;
    }
    job_ = job;
    return 0;
}

unsigned long JobThrottle::setLimit(std::uint64_t bytes_per_second) {
    return setRateControl(job_, bytes_per_second, true);
}

unsigned long JobThrottle::clearLimit() {
    return setRateControl(job_, 0, false);
}

}  // namespace byteleash::enforce_test
