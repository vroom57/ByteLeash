#include "etw_session.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
// ETW headers need windows.h first.
#include <evntcons.h>
#include <evntrace.h>
#include <tdh.h>

#include <cstddef>
#include <cstring>
#include <vector>

namespace byteleash::probe {

namespace {

// Microsoft-Windows-Kernel-Network
constexpr GUID kKernelNetwork = {
    0x7dd42a49, 0x5329, 0x4832, {0x8d, 0xfd, 0x43, 0xd9, 0x79, 0x15, 0x3a, 0x88}};
// Microsoft-Windows-DNS-Client
constexpr GUID kDnsClient = {
    0x1c95126e, 0x7eea, 0x49a9, {0xa3, 0xfe, 0xa3, 0x78, 0xb0, 0x3d, 0xdb, 0x4d}};

constexpr ULONGLONG kKeywordIpv4 = 0x10;
constexpr ULONGLONG kKeywordIpv6 = 0x20;
constexpr USHORT kDnsQueryCompleted = 3008;

constexpr wchar_t kSessionName[] = L"ByteLeashProbe";

// Kernel-Network event IDs: TCPv4, TCPv6, UDPv4, UDPv6.
bool isSend(USHORT id) {
    return id == 10 || id == 26 || id == 42 || id == 58;
}
bool isReceive(USHORT id) {
    return id == 11 || id == 27 || id == 43 || id == 59;
}
bool isUdp(USHORT id) {
    return id >= 42;
}

struct TraceProperties {
    EVENT_TRACE_PROPERTIES props;
    wchar_t name[64];
};

TraceProperties makeProperties() {
    TraceProperties t{};
    t.props.Wnode.BufferSize = sizeof(t);
    t.props.Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    t.props.Wnode.ClientContext = 1;  // QueryPerformanceCounter timestamps
    t.props.LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    t.props.LoggerNameOffset = offsetof(TraceProperties, name);
    t.props.BufferSize = 64;  // KB
    t.props.MinimumBuffers = 16;
    t.props.MaximumBuffers = 64;
    t.props.FlushTimer = 1;  // seconds
    return t;
}

void WINAPI eventRecordCallback(PEVENT_RECORD record) {
    static_cast<EtwSession*>(record->UserContext)->onEvent(record);
}

// Size of a fixed-size TDH field, or 0 if it's variable-size or unsupported.
std::size_t fieldSize(const EVENT_PROPERTY_INFO& prop, bool is32bit) {
    if (prop.Flags & (PropertyStruct | PropertyParamLength | PropertyParamCount)) {
        return 0;
    }
    std::size_t size = 0;
    switch (prop.nonStructType.InType) {
        case TDH_INTYPE_INT8:
        case TDH_INTYPE_UINT8:
            size = 1;
            break;
        case TDH_INTYPE_INT16:
        case TDH_INTYPE_UINT16:
            size = 2;
            break;
        case TDH_INTYPE_INT32:
        case TDH_INTYPE_UINT32:
        case TDH_INTYPE_HEXINT32:
        case TDH_INTYPE_BOOLEAN:
            size = 4;
            break;
        case TDH_INTYPE_INT64:
        case TDH_INTYPE_UINT64:
        case TDH_INTYPE_HEXINT64:
        case TDH_INTYPE_FILETIME:
            size = 8;
            break;
        case TDH_INTYPE_POINTER:
        case TDH_INTYPE_SIZET:
            size = is32bit ? 4 : 8;
            break;
        case TDH_INTYPE_GUID:
            size = 16;
            break;
        case TDH_INTYPE_BINARY:
            size = prop.length;
            break;
        default:
            return 0;
    }
    return size * (prop.count == 0 ? 1 : prop.count);
}

std::wstring infoString(const TRACE_EVENT_INFO* info, ULONG offset) {
    if (offset == 0) {
        return {};
    }
    return reinterpret_cast<const wchar_t*>(
        reinterpret_cast<const BYTE*>(info) + offset);
}

std::wstring stringProperty(PEVENT_RECORD record, const wchar_t* name) {
    PROPERTY_DATA_DESCRIPTOR desc{};
    desc.PropertyName = reinterpret_cast<ULONGLONG>(name);
    desc.ArrayIndex = ULONG_MAX;
    ULONG size = 0;
    if (TdhGetPropertySize(record, 0, nullptr, 1, &desc, &size) != ERROR_SUCCESS ||
        size < sizeof(wchar_t)) {
        return {};
    }
    std::wstring value(size / sizeof(wchar_t), L'\0');
    if (TdhGetProperty(record, 0, nullptr, 1, &desc, size,
                       reinterpret_cast<PBYTE>(value.data())) != ERROR_SUCCESS) {
        return {};
    }
    while (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    return value;
}

template <typename T>
T readAt(const BYTE* data, std::size_t offset) {
    T value;
    std::memcpy(&value, data + offset, sizeof(T));
    return value;
}

}  // namespace

EtwSession::EtwSession(NetHandler on_net, DnsHandler on_dns)
    : on_net_(std::move(on_net)), on_dns_(std::move(on_dns)) {}

EtwSession::~EtwSession() {
    stop();
    if (trace_ != INVALID_PROCESSTRACE_HANDLE) {
        CloseTrace(trace_);
    }
}

unsigned long EtwSession::start() {
    // A session left over from a crashed run would block StartTrace.
    TraceProperties props = makeProperties();
    ULONG err = StartTraceW(&session_, kSessionName, &props.props);
    if (err == ERROR_ALREADY_EXISTS) {
        TraceProperties old = makeProperties();
        ControlTraceW(0, kSessionName, &old.props, EVENT_TRACE_CONTROL_STOP);
        props = makeProperties();
        err = StartTraceW(&session_, kSessionName, &props.props);
    }
    if (err != ERROR_SUCCESS) {
        return err;
    }

    err = EnableTraceEx2(session_, &kKernelNetwork,
                         EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                         TRACE_LEVEL_INFORMATION, kKeywordIpv4 | kKeywordIpv6, 0,
                         0, nullptr);
    if (err != ERROR_SUCCESS) {
        return err;
    }
    // DNS names are a nice-to-have; carry on without them if this fails.
    EnableTraceEx2(session_, &kDnsClient, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                   TRACE_LEVEL_VERBOSE, ~0ull, 0, 0, nullptr);

    EVENT_TRACE_LOGFILEW log{};
    log.LoggerName = const_cast<LPWSTR>(kSessionName);
    log.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME |
                           PROCESS_TRACE_MODE_EVENT_RECORD |
                           PROCESS_TRACE_MODE_RAW_TIMESTAMP;
    log.EventRecordCallback = eventRecordCallback;
    log.Context = this;
    trace_ = OpenTraceW(&log);
    if (trace_ == INVALID_PROCESSTRACE_HANDLE) {
        return GetLastError();
    }
    return ERROR_SUCCESS;
}

unsigned long EtwSession::process() {
    TRACEHANDLE handle = trace_;
    return ProcessTrace(&handle, 1, nullptr, nullptr);
}

void EtwSession::stop() {
    if (stopped_ || session_ == 0) {
        return;
    }
    stopped_ = true;
    TraceProperties props = makeProperties();
    if (ControlTraceW(session_, nullptr, &props.props,
                      EVENT_TRACE_CONTROL_STOP) == ERROR_SUCCESS) {
        events_lost_ = props.props.EventsLost;
        buffers_lost_ =
            props.props.RealTimeBuffersLost + props.props.LogBuffersLost;
    }
}

void EtwSession::onEvent(void* raw) {
    auto* record = static_cast<PEVENT_RECORD>(raw);
    const GUID& provider = record->EventHeader.ProviderId;
    if (IsEqualGUID(provider, kKernelNetwork)) {
        onNetworkEvent(record);
    } else if (IsEqualGUID(provider, kDnsClient)) {
        onDnsEvent(record);
    }
}

void EtwSession::onNetworkEvent(void* raw) {
    auto* record = static_cast<PEVENT_RECORD>(raw);
    const USHORT id = record->EventHeader.EventDescriptor.Id;
    const Layout& layout = layoutFor(record);
    if (!layout.valid || (!isSend(id) && !isReceive(id))) {
        return;
    }

    const auto* data = static_cast<const BYTE*>(record->UserData);
    const std::size_t len = record->UserDataLength;
    if (layout.pid + 4 > len || layout.size + 4 > len ||
        layout.daddr + layout.daddr_len > len || layout.dport + 2 > len) {
        return;
    }

    NetEvent event;
    event.qpc = record->EventHeader.TimeStamp.QuadPart;
    event.pid = readAt<std::uint32_t>(data, layout.pid);
    event.size = readAt<std::uint32_t>(data, layout.size);
    event.protocol = isUdp(id) ? Protocol::Udp : Protocol::Tcp;
    event.direction = isSend(id) ? Direction::Send : Direction::Receive;
    event.ipv6 = layout.daddr_len == 16;
    if (event.direction == Direction::Send) {
        if (event.ipv6) {
            std::memcpy(event.remote6.data(), data + layout.daddr, 16);
        } else {
            std::memcpy(event.remote4.data(), data + layout.daddr, 4);
        }
        // Ports are logged in network byte order.
        event.remote_port =
            _byteswap_ushort(readAt<std::uint16_t>(data, layout.dport));
    }
    on_net_(event);
}

void EtwSession::onDnsEvent(void* raw) {
    auto* record = static_cast<PEVENT_RECORD>(raw);
    if (record->EventHeader.EventDescriptor.Id != kDnsQueryCompleted) {
        return;
    }
    DnsEvent event;
    event.pid = record->EventHeader.ProcessId;
    event.name = stringProperty(record, L"QueryName");
    event.results = stringProperty(record, L"QueryResults");
    if (!event.name.empty() && !event.results.empty()) {
        on_dns_(event);
    }
}

const EtwSession::Layout& EtwSession::layoutFor(void* raw) {
    auto* record = static_cast<PEVENT_RECORD>(raw);
    const auto& desc = record->EventHeader.EventDescriptor;
    EventIdStats& stats = ids_[desc.Id];
    ++stats.count;

    const auto key = std::make_pair(unsigned{desc.Id}, unsigned{desc.Version});
    if (auto it = layouts_.find(key); it != layouts_.end()) {
        return it->second;
    }
    Layout& layout = layouts_[key];

    ULONG size = 0;
    if (TdhGetEventInformation(record, 0, nullptr, nullptr, &size) !=
        ERROR_INSUFFICIENT_BUFFER) {
        return layout;
    }
    std::vector<BYTE> buffer(size);
    auto* info = reinterpret_cast<TRACE_EVENT_INFO*>(buffer.data());
    if (TdhGetEventInformation(record, 0, nullptr, info, &size) != ERROR_SUCCESS) {
        return layout;
    }
    stats.task = infoString(info, info->TaskNameOffset);
    stats.opcode = infoString(info, info->OpcodeNameOffset);

    const bool is32bit =
        (record->EventHeader.Flags & EVENT_HEADER_FLAG_32_BIT_HEADER) != 0;
    bool have_pid = false, have_size = false, have_daddr = false,
         have_dport = false;
    std::size_t offset = 0;
    for (ULONG i = 0; i < info->TopLevelPropertyCount; ++i) {
        const EVENT_PROPERTY_INFO& prop = info->EventPropertyInfoArray[i];
        const std::wstring name = infoString(info, prop.NameOffset);
        const std::size_t field = fieldSize(prop, is32bit);
        if (field == 0) {
            break;  // Can't know offsets past a variable-size field.
        }
        if (name == L"PID" && field == 4) {
            layout.pid = offset;
            have_pid = true;
        } else if (name == L"size" && field == 4) {
            layout.size = offset;
            have_size = true;
        } else if (name == L"daddr" && (field == 4 || field == 16)) {
            layout.daddr = offset;
            layout.daddr_len = field;
            have_daddr = true;
        } else if (name == L"dport" && field == 2) {
            layout.dport = offset;
            have_dport = true;
        }
        offset += field;
    }
    layout.valid = have_pid && have_size && have_daddr && have_dport;
    stats.decoded = layout.valid;
    return layout;
}

}  // namespace byteleash::probe
