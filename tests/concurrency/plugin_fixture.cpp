#include "../../pch.h"
#include "../../DataManager.h"
#include "../../ModuleTask.h"
#include <stdexcept>

struct ConcurrencyTestAccess
{
    static HANDLE WINAPI FailThread(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE,
        LPVOID, DWORD, LPDWORD)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return nullptr;
    }
    static HANDLE WINAPI ThrowThread(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE,
        LPVOID, DWORD, LPDWORD)
    {
        throw std::runtime_error("injected launch failure");
    }
    static void Failure(int mode)
    {
        ModuleTask::Detail::Factory() = mode == 1 ? &FailThread :
            mode == 2 ? &ThrowThread : &CreateThread;
    }
};

extern "C" __declspec(dllexport) void TestConfigure(const wchar_t* base, BOOL second)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    auto settings = g_data.GetSettings();
    settings.use_proxy = false;
    settings.api_domestic_url = std::wstring(base) + (second ? L"/ipB" : L"/ipA");
    settings.latency_targets = { { second ? L"B" : L"A",
        std::wstring(base) + (second ? L"/latencyB" : L"/latencyA") } };
    settings.display_format = DF_IP_ONLY;
    g_data.SetSettings(std::move(settings));
}

extern "C" __declspec(dllexport) void TestUpdate() { g_data.ForceUpdateAsync(); }
extern "C" __declspec(dllexport) BOOL TestBusy() { return g_data.IsUpdating(); }
extern "C" __declspec(dllexport) void TestSwap() { g_data.SwapBuffers(); }
extern "C" __declspec(dllexport) void TestFailure(int mode) { ConcurrencyTestAccess::Failure(mode); }
extern "C" __declspec(dllexport) const wchar_t* TestText() { return g_data.GetFormattedItemText(false); }
extern "C" __declspec(dllexport) const wchar_t* TestTooltip() { return g_data.GetTooltipText(); }
extern "C" __declspec(dllexport) int TestState()
{
    const auto result = g_data.GetResults();
    if (result.m_public_ip_direct.empty()) return 0;
    if (result.m_latency_results.size() != 1) return -1;
    const bool second = result.m_public_ip_direct == L"198.51.100.2";
    if (!second && result.m_public_ip_direct != L"198.51.100.1") return -2;
    const auto& latency = result.m_latency_results.front();
    const std::wstring suffix = second ? L"/latencyB" : L"/latencyA";
    if (latency.name != (second ? L"B" : L"A") ||
        latency.url.size() < suffix.size() ||
        latency.url.compare(latency.url.size() - suffix.size(), suffix.size(), suffix) != 0)
        return -3;
    return second ? 2 : 1;
}
