#include <winsock2.h>
#include <Windows.h>
#pragma warning(push)
#pragma warning(disable: 4100) // Unused parameters in the unmodified upstream interface.
#include "../../include/PluginInterface.h"
#pragma warning(pop)
#include <cstdio>
#include <string>
#include <stdexcept>
#include <atomic>
#include <thread>
#pragma comment(lib, "ws2_32.lib")

static void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

class Server
{
    SOCKET listener{ INVALID_SOCKET };
    std::thread worker;
    std::atomic<bool> stopping{ false };
public:
    std::wstring base;
    Server()
    {
        WSADATA data{};
        Check(WSAStartup(MAKEWORD(2, 2), &data) == 0, "WSAStartup");
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        Check(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "bind");
        int size = sizeof(address);
        Check(getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size) == 0, "getsockname");
        Check(listen(listener, 4) == 0, "listen");
        base = L"http://127.0.0.1:" + std::to_wstring(ntohs(address.sin_port));
        worker = std::thread([this]() {
            while (!stopping)
            {
                SOCKET client = accept(listener, nullptr, nullptr);
                if (client == INVALID_SOCKET) break;
                char request[4096]{};
                const int read = recv(client, request, sizeof(request) - 1, 0);
                const bool get = read > 0 && std::string(request, read).find("GET ") == 0;
                const std::string body = get ?
                    "{\"status\":\"success\",\"query\":\"198.51.100.1\",\"country\":\"\\u4e2d\\u56fd \\u4e0a\\u6d77\",\"isp\":\"Test ISP\"}" : "";
                const std::string response = "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: " +
                    std::to_string(body.size()) + "\r\n\r\n" + body;
                send(client, response.c_str(), static_cast<int>(response.size()), 0);
                closesocket(client);
            }
        });
    }
    ~Server() { stopping = true; closesocket(listener); worker.join(); WSACleanup(); }
};

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
        Server server;
        const HMODULE module = LoadLibraryW(argv[1]);
        Check(module != nullptr, "LoadLibrary");
        using Instance = ITMPlugin* (*)();
        using Smoke = BOOL (*)(BOOL);
        using Configure = void (*)(const wchar_t*, BOOL);
        using Update = void (*)();
        using Busy = BOOL (*)();
        const auto instance = reinterpret_cast<Instance>(GetProcAddress(module, "TMPluginGetInstance"));
        const auto smoke = reinterpret_cast<Smoke>(GetProcAddress(module, "TestLocalizedDialogs"));
        const auto configure = reinterpret_cast<Configure>(GetProcAddress(module, "TestConfigure"));
        const auto update = reinterpret_cast<Update>(GetProcAddress(module, "TestUpdate"));
        const auto busy = reinterpret_cast<Busy>(GetProcAddress(module, "TestBusy"));
        const auto swap = reinterpret_cast<Update>(GetProcAddress(module, "TestSwap"));
        Check(instance && smoke && configure && update && busy && swap, "exports");
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
            Check(!busy(), "OnInitialize without config started a request");
            Check(plugin->GetItem(0)->GetItemLableText() == std::wstring(chinese ? L"直:" : L"Direct:"), "item label language");
            configure(server.base.c_str(), FALSE);
            update();
            for (int index = 0; index < 500 && busy(); ++index) Sleep(10);
            Check(!busy(), "loopback update timeout");
            swap();
            const std::wstring tooltip = plugin->GetTooltipInfo();
            Check(tooltip.find(chinese ? L"直: " : L"Direct: ") == 0, "tooltip language");
            Check(tooltip.find(L"中国 上海") != std::wstring::npos, "API geography was translated");
            Check(tooltip.find(chinese ? L"最后更新:" : L"Last updated:") != std::wstring::npos, "tooltip status language");
            Check(smoke(chinese) != FALSE, "native dialog resource selection");
            Check(GetThreadUILanguage() == threadLanguage, "host thread language changed");
            std::printf("PASS: host language %04x, native templates, metadata, retained pointer, thread locale\n", language);
        }
        FreeLibrary(module);
        return 0;
    }
    catch (const std::exception& error) { std::printf("FAIL: %s\n", error.what()); return 1; }
}
