#include <Windows.h>
#pragma warning(push)
#pragma warning(disable: 4100) // Unused parameters in the unmodified upstream interface.
#include "../../include/PluginInterface.h"
#pragma warning(pop)
#include <cstdio>
#include <string>
#include <stdexcept>

static void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

class Host : public ITrafficMonitor
{
public:
    unsigned short language{ 0x0409 };
    int GetAPIVersion() override { return 0; }
    const wchar_t* GetVersion() override { return L"1.86"; }
    double GetMonitorValue(MonitorItem) override { return 0; }
    const wchar_t* GetMonitorValueString(MonitorItem, int) override { return L""; }
    void ShowNotifyMessage(const wchar_t*) override {}
    unsigned short GetLanguageId() const override { return language; }
    const wchar_t* GetPluginConfigDir() const override { return L""; }
    int GetDPI(DPIType) const override { return 96; }
    unsigned int GetThemeColor() const override { return 0; }
    const wchar_t* GetStringRes(const wchar_t*, const wchar_t*) override { return L""; }
    void* GetMainWindowHwnd() override { return nullptr; }
    void* GetTaskbarWindowHwnd() override { return nullptr; }
};

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2) return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    try
    {
        const HMODULE module = LoadLibraryW(argv[1]);
        Check(module != nullptr, "LoadLibrary");
        using Instance = ITMPlugin* (*)();
        using Smoke = BOOL (*)(BOOL);
        const auto instance = reinterpret_cast<Instance>(GetProcAddress(module, "TMPluginGetInstance"));
        const auto smoke = reinterpret_cast<Smoke>(GetProcAddress(module, "TestLocalizedDialogs"));
        Check(instance && smoke, "exports");
        ITMPlugin* plugin = instance();
        const wchar_t* before = plugin->GetInfo(ITMPlugin::TMI_NAME);
        const std::wstring retained(before);
        Host host;
        const unsigned short languages[]{ 0x0409, 0x0804, 0x0404, 0x0419 };
        for (const auto language : languages)
        {
            host.language = language;
            const bool chinese = PRIMARYLANGID(language) == LANG_CHINESE;
            SetThreadUILanguage(chinese ? 0x0409 : 0x0804);
            const LANGID threadLanguage = GetThreadUILanguage();
            plugin->OnInitialize(&host);
            Check(plugin->GetInfo(ITMPlugin::TMI_NAME) ==
                std::wstring(chinese ? L"IP 地区监控插件" : L"IP Region Monitor"), "metadata language");
            Check(retained == before, "resource pointer invalidated by initialization");
            Check(smoke(chinese) != FALSE, "native dialog resource selection");
            Check(GetThreadUILanguage() == threadLanguage, "host thread language changed");
            std::printf("PASS: host language %04x, native templates, metadata, retained pointer, thread locale\n", language);
        }
        FreeLibrary(module);
        return 0;
    }
    catch (const std::exception& error) { std::printf("FAIL: %s\n", error.what()); return 1; }
}
