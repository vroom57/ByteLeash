#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#include <rpc.h>
#include <initguid.h>
#include <fwpmu.h>

#include "wfp.h"

#include <cwchar>
#include <initializer_list>

namespace byteleash::enforce_test {
namespace {

wchar_t g_name[] = L"ByteLeash test";

DWORD addBlockFilter(HANDLE engine, const GUID& layer,
                     FWPM_FILTER_CONDITION0* conditions, UINT32 count, UINT64& id) {
    FWPM_FILTER0 filter{};
    filter.displayData.name = g_name;
    filter.layerKey = layer;
    filter.action.type = FWP_ACTION_BLOCK;
    filter.weight.type = FWP_UINT8;
    filter.weight.uint8 = 15;
    filter.numFilterConditions = count;
    filter.filterCondition = conditions;
    return FwpmFilterAdd0(engine, &filter, nullptr, &id);
}

}  // namespace

WfpSession::~WfpSession() {
    if (engine_ != nullptr) {
        FwpmEngineClose0(engine_);
    }
}

unsigned long WfpSession::open() {
    FWPM_SESSION0 session{};
    session.flags = FWPM_SESSION_FLAG_DYNAMIC;
    session.displayData.name = g_name;
    HANDLE engine = nullptr;
    const DWORD err =
        FwpmEngineOpen0(nullptr, RPC_C_AUTHN_WINNT, nullptr, &session, &engine);
    if (err == ERROR_SUCCESS) {
        engine_ = engine;
    }
    return err;
}

unsigned long WfpSession::blockApp(const std::wstring& exe_path) {
    FWP_BYTE_BLOB* app_id = nullptr;
    DWORD err = FwpmGetAppIdFromFileName0(exe_path.c_str(), &app_id);
    if (err != ERROR_SUCCESS) {
        return err;
    }
    FWPM_FILTER_CONDITION0 condition{};
    condition.fieldKey = FWPM_CONDITION_ALE_APP_ID;
    condition.matchType = FWP_MATCH_EQUAL;
    condition.conditionValue.type = FWP_BYTE_BLOB_TYPE;
    condition.conditionValue.byteBlob = app_id;
    for (const GUID& layer :
         {FWPM_LAYER_ALE_AUTH_CONNECT_V4, FWPM_LAYER_ALE_AUTH_CONNECT_V6}) {
        UINT64 id = 0;
        err = addBlockFilter(engine_, layer, &condition, 1, id);
        if (err != ERROR_SUCCESS) {
            break;
        }
        filters_.push_back(id);
    }
    FwpmFreeMemory0(reinterpret_cast<void**>(&app_id));
    return err;
}

unsigned long WfpSession::blockConnection(const Endpoint& remote,
                                          std::uint16_t local_port) {
    FWPM_FILTER_CONDITION0 conditions[4]{};
    conditions[0].fieldKey = FWPM_CONDITION_IP_PROTOCOL;
    conditions[0].conditionValue.type = FWP_UINT8;
    conditions[0].conditionValue.uint8 = static_cast<UINT8>(IPPROTO_TCP);
    conditions[1].fieldKey = FWPM_CONDITION_IP_REMOTE_ADDRESS;
    conditions[1].conditionValue.type = FWP_UINT32;
    conditions[1].conditionValue.uint32 = remote.address;
    conditions[2].fieldKey = FWPM_CONDITION_IP_REMOTE_PORT;
    conditions[2].conditionValue.type = FWP_UINT16;
    conditions[2].conditionValue.uint16 = remote.port;
    conditions[3].fieldKey = FWPM_CONDITION_IP_LOCAL_PORT;
    conditions[3].conditionValue.type = FWP_UINT16;
    conditions[3].conditionValue.uint16 = local_port;
    for (auto& condition : conditions) {
        condition.matchType = FWP_MATCH_EQUAL;
    }
    UINT64 id = 0;
    const DWORD err =
        addBlockFilter(engine_, FWPM_LAYER_OUTBOUND_TRANSPORT_V4, conditions, 4, id);
    if (err == ERROR_SUCCESS) {
        filters_.push_back(id);
    }
    return err;
}

unsigned long WfpSession::removeAll() {
    DWORD result = ERROR_SUCCESS;
    for (const UINT64 id : filters_) {
        if (const DWORD err = FwpmFilterDeleteById0(engine_, id); err != ERROR_SUCCESS) {
            result = err;
        }
    }
    filters_.clear();
    return result;
}

unsigned long deleteLeftoverFilters(int& found) {
    found = 0;
    HANDLE engine = nullptr;
    DWORD err = FwpmEngineOpen0(nullptr, RPC_C_AUTHN_WINNT, nullptr, nullptr, &engine);
    if (err != ERROR_SUCCESS) {
        return err;
    }
    std::vector<UINT64> ids;
    HANDLE enum_handle = nullptr;
    err = FwpmFilterCreateEnumHandle0(engine, nullptr, &enum_handle);
    if (err == ERROR_SUCCESS) {
        constexpr UINT32 kBatch = 256;
        while (true) {
            FWPM_FILTER0** entries = nullptr;
            UINT32 count = 0;
            err = FwpmFilterEnum0(engine, enum_handle, kBatch, &entries, &count);
            if (err != ERROR_SUCCESS) {
                break;
            }
            for (UINT32 i = 0; i < count; ++i) {
                const wchar_t* name = entries[i]->displayData.name;
                if (name != nullptr && std::wcscmp(name, g_name) == 0) {
                    ids.push_back(entries[i]->filterId);
                }
            }
            FwpmFreeMemory0(reinterpret_cast<void**>(&entries));
            if (count < kBatch) {
                break;
            }
        }
        FwpmFilterDestroyEnumHandle0(engine, enum_handle);
    }
    for (const UINT64 id : ids) {
        FwpmFilterDeleteById0(engine, id);
    }
    found = static_cast<int>(ids.size());
    FwpmEngineClose0(engine);
    return err;
}

}  // namespace byteleash::enforce_test
