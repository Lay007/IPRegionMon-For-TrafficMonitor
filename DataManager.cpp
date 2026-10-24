#include "pch.h"
#include "DataManager.h"
#include <winhttp.h>
#include <stdarg.h>
#include <fstream>
#include <thread>
#include <future>
#pragma comment(lib, "winhttp.lib")

// declare __ImageBase to get current module path when used inside DLL
extern "C" IMAGE_DOS_HEADER __ImageBase;

CDataManager CDataManager::m_instance;

static std::wstring Utf8ToWide(const std::string& utf8)
{
    if (utf8.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
    std::wstring w; w.resize(len);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &w[0], len);
    return w;
}

static std::string WideToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s; s.resize(len);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], len, nullptr, nullptr);
    return s;
}

static std::wstring Utf8HexEncode(const std::wstring& w)
{
    if (w.empty()) return L"";
    std::string u8 = WideToUtf8(w);
    static const wchar_t* hex = L"0123456789ABCDEF";
    std::wstring out; out.reserve(4 + u8.size() * 2);
    out += L"#u8:";
    for (unsigned char c : u8)
    {
        out.push_back(hex[(c >> 4) & 0xF]);
        out.push_back(hex[c & 0xF]);
    }
    return out;
}

static std::wstring Utf8HexDecode(const std::wstring& s)
{
    if (s.size() < 4 || s.substr(0, 4) != L"#u8:") return s;
    auto hex2 = [](wchar_t ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        return 0;
    };
    std::string bytes; bytes.reserve((s.size() - 4) / 2);
    for (size_t i = 4; i + 1 < s.size(); i += 2)
    {
        unsigned char b = (unsigned char)((hex2(s[i]) << 4) | hex2(s[i + 1]));
        bytes.push_back((char)b);
    }
    return Utf8ToWide(bytes);
}

static std::wstring DecodeJsonString(const std::wstring& s)
{
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == L'\\' && i + 1 < s.size())
        {
            wchar_t ch = s[i + 1];
            if (ch == L'u' && i + 5 < s.size())
            {
                wchar_t val = 0;
                for (int j = 0; j < 4; ++j)
                {
                    wchar_t hc = s[i + 2 + j];
                    val <<= 4;
                    if (hc >= L'0' && hc <= L'9') val |= (hc - L'0');
                    else if (hc >= L'A' && hc <= L'F') val |= (hc - L'A' + 10);
                    else if (hc >= L'a' && hc <= L'f') val |= (hc - L'a' + 10);
                }
                out.push_back(val);
                i += 5;
            }
            else
            {
                if (ch == L'"') out.push_back(L'"');
                else if (ch == L'\\') out.push_back(L'\\');
                else if (ch == L'/') out.push_back(L'/');
                else if (ch == L'b') out.push_back(L'\b');
                else if (ch == L'f') out.push_back(L'\f');
                else if (ch == L'n') out.push_back(L'\n');
                else if (ch == L'r') out.push_back(L'\r');
                else if (ch == L't') out.push_back(L'\t');
                else out.push_back(ch);
                i += 1;
            }
        }
        else
        {
            out.push_back(s[i]);
        }
    }
    return out;
}

// 朴素JSON字段提取（支持转义字符与Unicode解码）
static std::wstring GetJsonStringValue(const std::wstring& json, const wchar_t* key)
{
    std::wstring keyQuoted = L"\"" + std::wstring(key) + L"\"";
    size_t kpos = json.find(keyQuoted);
    if (kpos == std::wstring::npos) return L"";
    size_t colon = json.find(L':', kpos + keyQuoted.size());
    if (colon == std::wstring::npos) return L"";
    // 跳过空白
    size_t pos = colon + 1;
    while (pos < json.size() && iswspace(json[pos])) ++pos;
    if (pos >= json.size()) return L"";
    if (json[pos] == L'"')
    {
        size_t start = pos + 1;
        size_t end = start;
        while (end < json.size())
        {
            if (json[end] == L'"')
            {
                size_t slashes = 0;
                size_t p = end - 1;
                while (p >= start && json[p] == L'\\') { ++slashes; --p; }
                if (slashes % 2 == 0) break;
            }
            ++end;
        }
        if (end >= json.size()) return L"";
        return DecodeJsonString(json.substr(start, end - start));
    }
    size_t end = pos;
    while (end < json.size() && json[end] != L',' && json[end] != L'}' && json[end] != L']') ++end;
    std::wstring raw = json.substr(pos, end - pos);
    size_t trim_s = 0, trim_e = raw.size();
    while (trim_s < raw.size() && iswspace(raw[trim_s])) ++trim_s;
    while (trim_e > trim_s && iswspace(raw[trim_e - 1])) --trim_e;
    return DecodeJsonString(raw.substr(trim_s, trim_e - trim_s));
}

static bool GetJsonBoolValue(const std::wstring& json, const wchar_t* key)
{
    std::wstring val = GetJsonStringValue(json, key);
    return (val == L"true" || val == L"1");
}

static std::wstring GetJsonObjectValue(const std::wstring& json, const wchar_t* key)
{
    std::wstring keyQuoted = L"\"" + std::wstring(key) + L"\"";
    size_t kpos = json.find(keyQuoted);
    if (kpos == std::wstring::npos) return L"";

    size_t colon = json.find(L':', kpos + keyQuoted.size());
    if (colon == std::wstring::npos) return L"";

    size_t pos = colon + 1;
    while (pos < json.size() && iswspace(json[pos])) ++pos;
    if (pos >= json.size() || json[pos] != L'{') return L"";

    int depth = 1;
    size_t end = pos + 1;
    while (end < json.size() && depth > 0)
    {
        if      (json[end] == L'{') ++depth;
        else if (json[end] == L'}') --depth;
        ++end;
    }
    return json.substr(pos, end - pos);
}

static std::vector<std::wstring> GetJsonArrayStrings(const std::wstring& json, const wchar_t* key)
{
    std::vector<std::wstring> result;
    std::wstring keyQuoted = L"\"" + std::wstring(key) + L"\"";
    size_t kpos = json.find(keyQuoted);
    if (kpos == std::wstring::npos) return result;

    size_t colon = json.find(L':', kpos + keyQuoted.size());
    if (colon == std::wstring::npos) return result;

    size_t pos = colon + 1;
    while (pos < json.size() && iswspace(json[pos])) ++pos;
    if (pos >= json.size() || json[pos] != L'[') return result;

    size_t end = json.find(L']', pos + 1);
    if (end == std::wstring::npos) return result;

    size_t cur = pos + 1;
    while (cur < end)
    {
        while (cur < end && json[cur] != L'"') ++cur;
        if (cur >= end) break;
        size_t strStart = cur + 1;
        size_t strEnd = strStart;
        while (strEnd < end)
        {
            if (json[strEnd] == L'"')
            {
                size_t slashes = 0;
                size_t p = strEnd - 1;
                while (p >= strStart && json[p] == L'\\') { ++slashes; --p; }
                if (slashes % 2 == 0) break;
            }
            ++strEnd;
        }
        if (strEnd <= end)
        {
            result.push_back(DecodeJsonString(json.substr(strStart, strEnd - strStart)));
            cur = strEnd + 1;
        }
        else break;
    }
    return result;
}


// 通过 WinHTTP 进行 GET 请求
static bool HttpGet(const std::wstring& url, const std::wstring& proxy, bool no_proxy, std::string& out)
{
    URL_COMPONENTSW uc{}; uc.dwStructSize = sizeof(uc);
    wchar_t host[256]{}; wchar_t path[2048]{}; wchar_t scheme[16]{};
    uc.lpszHostName = host; uc.dwHostNameLength = _countof(host);
    uc.lpszUrlPath = path; uc.dwUrlPathLength = _countof(path);
    uc.lpszScheme = scheme; uc.dwSchemeLength = _countof(scheme);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc))
        return false;
    bool https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    std::wstring sHost(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring sPath = (uc.dwUrlPathLength > 0) ? std::wstring(uc.lpszUrlPath, uc.dwUrlPathLength) : L"/";

    DWORD accessType = WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
    const wchar_t* pProxy = WINHTTP_NO_PROXY_NAME;
    if (!proxy.empty())
    {
        accessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
        pProxy = proxy.c_str();
    }
    else if (no_proxy)
    {
        accessType = WINHTTP_ACCESS_TYPE_NO_PROXY;
    }

    HINTERNET hs = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
        accessType, pProxy, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hs) return false;

    // 连接与读取超时设为 4000ms，增强容错
    WinHttpSetTimeouts(hs, 4000, 4000, 4000, 4000);

    DWORD maxRetries = 1;
    WinHttpSetOption(hs, WINHTTP_OPTION_CONNECT_RETRIES, &maxRetries, sizeof(maxRetries));

    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(hs, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    HINTERNET hc = WinHttpConnect(hs, sHost.c_str(), uc.nPort, 0);
    if (!hc) { WinHttpCloseHandle(hs); return false; }

    DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hr = WinHttpOpenRequest(hc, L"GET", sPath.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hr) { WinHttpCloseHandle(hc); WinHttpCloseHandle(hs); return false; }

    // 自动跟踪重定向与放宽安全证书限制（兼容代理中间人/内网环境）
    DWORD redirectOption = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hr, WINHTTP_OPTION_REDIRECT_POLICY, &redirectOption, sizeof(redirectOption));
    if (https)
    {
        DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                         SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(hr, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
    }

    BOOL ok = WinHttpSendRequest(hr, L"Connection: close\r\n", -1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (ok) ok = WinHttpReceiveResponse(hr, nullptr);

    if (ok)
    {
        DWORD sz = 0;
        do {
            DWORD got = 0;
            if (!WinHttpQueryDataAvailable(hr, &sz) || sz == 0) break;
            std::string buf; buf.resize(sz);
            if (!WinHttpReadData(hr, &buf[0], sz, &got)) break;
            if (got == 0) break;
            buf.resize(got);
            out += buf;
        } while (sz > 0);
    }

    WinHttpCloseHandle(hr); WinHttpCloseHandle(hc); WinHttpCloseHandle(hs);
    return !out.empty();
}

bool CDataManager::MeasureLatencyMs(const std::wstring& url_or_host, const std::wstring& proxy, bool no_proxy, double& out_ms)
{
    out_ms = -1.0;
    std::wstring url = url_or_host;
    if (url.find(L"://") == std::wstring::npos)
        url = L"https://" + url;
    URL_COMPONENTSW uc{}; uc.dwStructSize = sizeof(uc);
    wchar_t host[256]{}; wchar_t path[2048]{}; wchar_t scheme[16]{};
    uc.lpszHostName = host; uc.dwHostNameLength = _countof(host);
    uc.lpszUrlPath = path; uc.dwUrlPathLength = _countof(path);
    uc.lpszScheme = scheme; uc.dwSchemeLength = _countof(scheme);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc))
        return false;
    bool https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    std::wstring sHost(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring sPath = (uc.dwUrlPathLength > 0) ? std::wstring(uc.lpszUrlPath, uc.dwUrlPathLength) : L"/";

    DWORD accessType = WINHTTP_ACCESS_TYPE_DEFAULT_PROXY;
    const wchar_t* pProxy = WINHTTP_NO_PROXY_NAME;
    if (!proxy.empty())
    {
        accessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
        pProxy = proxy.c_str();
    }
    else if (no_proxy)
    {
        accessType = WINHTTP_ACCESS_TYPE_NO_PROXY;
    }

    HINTERNET hs = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
        accessType, pProxy, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hs) return false;

    // 测速超时 1200ms
    WinHttpSetTimeouts(hs, 1200, 1200, 1200, 1200);

    DWORD maxRetries = 1;
    WinHttpSetOption(hs, WINHTTP_OPTION_CONNECT_RETRIES, &maxRetries, sizeof(maxRetries));

    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(hs, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    HINTERNET hc = WinHttpConnect(hs, sHost.c_str(), uc.nPort, 0);
    if (!hc) { WinHttpCloseHandle(hs); return false; }

    DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hr = WinHttpOpenRequest(hc, L"HEAD", sPath.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hr) { WinHttpCloseHandle(hc); WinHttpCloseHandle(hs); return false; }

    if (https)
    {
        DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                         SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        WinHttpSetOption(hr, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
    }

    ULONGLONG t0 = GetTickCount64();
    BOOL ok = WinHttpSendRequest(hr, L"Connection: close\r\n", -1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (ok) ok = WinHttpReceiveResponse(hr, nullptr);
    ULONGLONG t1 = GetTickCount64();

    if (ok)
        out_ms = static_cast<double>(t1 - t0);

    WinHttpCloseHandle(hr); WinHttpCloseHandle(hc); WinHttpCloseHandle(hs);
    return ok == TRUE;
}

bool CDataManager::TestProxyConnectivity(const std::wstring& proxy, std::wstring& out_message, double& out_ms)
{
    out_ms = -1.0;
    if (proxy.empty())
    {
        out_message = L"代理地址为空！";
        return false;
    }

    // 探测 www.cloudflare.com
    bool ok = MeasureLatencyMs(L"https://www.cloudflare.com", proxy, false, out_ms);
    if (!ok || out_ms < 0)
    {
        ok = MeasureLatencyMs(L"https://www.google.com", proxy, false, out_ms);
    }

    if (ok && out_ms >= 0)
    {
        wchar_t buf[64]{};
        swprintf_s(buf, L"连接成功 (延迟: %.0f ms)", out_ms);
        out_message = buf;
        return true;
    }
    else
    {
        out_message = L"连接失败：无法通过该代理访问目标网络";
        return false;
    }
}

CDataManager::CDataManager()
{
    const wchar_t* kDomesticApi = L"http://ip-api.com/json/?fields=status,message,country,regionName,city,isp,query,proxy,hosting&lang=zh-CN";
    const wchar_t* kForeignApi  = L"http://ip-api.com/json/?fields=status,message,country,regionName,city,isp,query,proxy,hosting&lang=zh-CN";
    m_setting_data.api_domestic_url = kDomesticApi;
    m_setting_data.api_foreign_url  = kForeignApi;
    m_setting_data.use_proxy = false;
    m_setting_data.refresh_interval_sec = 300;
    m_setting_data.show_ip = true;
    m_setting_data.display_format = DF_REGION_AND_IP;

    // 默认测试目标
    m_setting_data.latency_targets.push_back({ L"百度", L"https://www.baidu.com" });
    m_setting_data.latency_targets.push_back({ L"Google", L"https://www.google.com" });
    m_setting_data.latency_targets.push_back({ L"Cloudflare", L"https://1.1.1.1" });

    m_ip_update_interval_ms = m_setting_data.refresh_interval_sec * 1000u;
}

CDataManager::~CDataManager()
{
    SaveConfig();
}

CDataManager& CDataManager::Instance()
{
    return m_instance;
}

void CDataManager::LoadConfig(const std::wstring& config_dir)
{
    HMODULE hModule = reinterpret_cast<HMODULE>(&__ImageBase);
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(hModule, path, MAX_PATH);
    std::wstring module_path = path;
    m_config_path = module_path;
    if (!config_dir.empty())
    {
        size_t index = module_path.find_last_of(L"\\/");
        std::wstring module_file_name = module_path.substr(index + 1);
        m_config_path = config_dir + module_file_name;
    }
    m_config_path += L".ini";

    m_setting_data.show_second = GetPrivateProfileIntW(L"config", L"show_second", 0, m_config_path.c_str());
    m_setting_data.use_proxy = GetPrivateProfileIntW(L"ip", L"use_proxy", 0, m_config_path.c_str()) != 0;

    wchar_t buff[1024]{};
    GetPrivateProfileStringW(L"ip", L"proxy_address", L"127.0.0.1:7890", buff, _countof(buff), m_config_path.c_str());
    m_setting_data.proxy_address = buff;

    m_setting_data.refresh_interval_sec = GetPrivateProfileIntW(L"ip", L"refresh_interval_sec", 300, m_config_path.c_str());
    m_setting_data.show_ip = GetPrivateProfileIntW(L"ip", L"show_ip", 1, m_config_path.c_str()) != 0;

    // 读取显示格式（若无则根据旧版 show_ip 推断）
    int def_format = m_setting_data.show_ip ? DF_REGION_AND_IP : DF_REGION_ONLY;
    m_setting_data.display_format = GetPrivateProfileIntW(L"ip", L"display_format", def_format, m_config_path.c_str());

    GetPrivateProfileStringW(L"ip", L"api_url", L"", buff, _countof(buff), m_config_path.c_str());
    m_setting_data.api_url = buff;

    GetPrivateProfileStringW(L"ip", L"api_domestic_url", L"", buff, _countof(buff), m_config_path.c_str());
    std::wstring domestic = buff;
    GetPrivateProfileStringW(L"ip", L"api_foreign_url", L"", buff, _countof(buff), m_config_path.c_str());
    std::wstring foreign = buff;

    // 读取延迟目标列表
    m_setting_data.latency_targets.clear();
    int n = GetPrivateProfileIntW(L"ip", L"latency_targets_n", 0, m_config_path.c_str());
    if (n > 0)
    {
        for (int i = 0; i < n; ++i)
        {
            wchar_t key_name[64]{}; swprintf_s(key_name, L"latency_name_%d", i);
            wchar_t key_url[64]{};  swprintf_s(key_url,  L"latency_url_%d", i);
            GetPrivateProfileStringW(L"ip", key_name, L"", buff, _countof(buff), m_config_path.c_str());
            std::wstring name = Utf8HexDecode(buff);
            GetPrivateProfileStringW(L"ip", key_url, L"", buff, _countof(buff), m_config_path.c_str());
            std::wstring url = Utf8HexDecode(buff);
            if (!url.empty())
                m_setting_data.latency_targets.push_back({ name, url });
        }
    }
    else
    {
        // 兼容单项 latency_target 或默认
        GetPrivateProfileStringW(L"ip", L"latency_target", L"", buff, _countof(buff), m_config_path.c_str());
        std::wstring single_latency = Utf8HexDecode(buff);
        if (!single_latency.empty())
        {
            m_setting_data.latency_targets.push_back({ L"默认", single_latency });
        }
        else
        {
            m_setting_data.latency_targets.push_back({ L"百度", L"https://www.baidu.com" });
            m_setting_data.latency_targets.push_back({ L"Google", L"https://www.google.com" });
            m_setting_data.latency_targets.push_back({ L"Cloudflare", L"https://1.1.1.1" });
        }
    }

    if (!domestic.empty()) m_setting_data.api_domestic_url = domestic; else if (!m_setting_data.api_url.empty()) m_setting_data.api_domestic_url = m_setting_data.api_url;
    if (!foreign.empty())  m_setting_data.api_foreign_url  = foreign;  else if (!m_setting_data.api_url.empty()) m_setting_data.api_foreign_url  = m_setting_data.api_url;

    const wchar_t* kDomesticApi = L"http://ip-api.com/json/?fields=status,message,country,regionName,city,isp,query,proxy,hosting&lang=zh-CN";
    const wchar_t* kForeignApi  = L"http://ip-api.com/json/?fields=status,message,country,regionName,city,isp,query,proxy,hosting&lang=zh-CN";
    if (m_setting_data.api_domestic_url.empty() || m_setting_data.api_domestic_url.find(L"ipip.net") != std::wstring::npos)
        m_setting_data.api_domestic_url = kDomesticApi;
    if (m_setting_data.api_foreign_url.empty())  m_setting_data.api_foreign_url  = kForeignApi;

    if (m_setting_data.refresh_interval_sec < 5) m_setting_data.refresh_interval_sec = 5;
    m_ip_update_interval_ms = (unsigned int)m_setting_data.refresh_interval_sec * 1000u;
}

static void WritePrivateProfileInt(const wchar_t* app, const wchar_t* key, int v, const wchar_t* file)
{
    wchar_t b[32]; swprintf_s(b, L"%d", v); WritePrivateProfileStringW(app, key, b, file);
}

void CDataManager::SaveConfig() const
{
    WritePrivateProfileInt(L"config", L"show_second", m_setting_data.show_second, m_config_path.c_str());
    WritePrivateProfileInt(L"ip", L"use_proxy", m_setting_data.use_proxy ? 1 : 0, m_config_path.c_str());
    WritePrivateProfileStringW(L"ip", L"proxy_address", m_setting_data.proxy_address.c_str(), m_config_path.c_str());
    WritePrivateProfileStringW(L"ip", L"api_domestic_url", m_setting_data.api_domestic_url.c_str(), m_config_path.c_str());
    WritePrivateProfileStringW(L"ip", L"api_foreign_url", m_setting_data.api_foreign_url.c_str(), m_config_path.c_str());
    WritePrivateProfileInt(L"ip", L"refresh_interval_sec", m_setting_data.refresh_interval_sec, m_config_path.c_str());
    WritePrivateProfileInt(L"ip", L"show_ip", m_setting_data.show_ip ? 1 : 0, m_config_path.c_str());
    WritePrivateProfileInt(L"ip", L"display_format", m_setting_data.display_format, m_config_path.c_str());

    WritePrivateProfileStringW(L"ip", L"latency_target", NULL, m_config_path.c_str());
    WritePrivateProfileInt(L"ip", L"latency_targets_n", (int)m_setting_data.latency_targets.size(), m_config_path.c_str());
    for (int i = 0; i < (int)m_setting_data.latency_targets.size(); ++i)
    {
        wchar_t key_name[64]{}; swprintf_s(key_name, L"latency_name_%d", i);
        wchar_t key_url[64]{};  swprintf_s(key_url,  L"latency_url_%d", i);
        std::wstring encName = Utf8HexEncode(m_setting_data.latency_targets[i].name);
        std::wstring encUrl  = Utf8HexEncode(m_setting_data.latency_targets[i].url);
        WritePrivateProfileStringW(L"ip", key_name, encName.c_str(), m_config_path.c_str());
        WritePrivateProfileStringW(L"ip", key_url,  encUrl.c_str(),  m_config_path.c_str());
    }
}

const wchar_t* CDataManager::StringRes(UINT id)
{
    auto it = m_string_table.find(id);
    if (it != m_string_table.end()) return it->second.c_str();
#ifdef AFX_MANAGE_STATE
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    HINSTANCE hResInst = AfxGetResourceHandle();
#else
    HINSTANCE hResInst = reinterpret_cast<HINSTANCE>(&__ImageBase);
#endif
    wchar_t buf[256]{};
    if (::LoadStringW(hResInst, id, buf, _countof(buf)) > 0) { m_string_table[id] = buf; return m_string_table[id].c_str(); }
    static const wchar_t* empty = L""; return empty;
}

bool CDataManager::FetchIpInfoByUrl(const std::wstring& url, bool use_proxy, std::wstring& out_place, std::wstring& out_ip, std::wstring& out_isp, IpThreatInfo& out_threat)
{
    std::string resp;
    std::wstring proxy = use_proxy ? m_setting_data.proxy_address : L"";
    bool no_proxy = !use_proxy;
    if (!HttpGet(url, proxy, no_proxy, resp))
    {
        out_place = L"请求失败（连接超时或网络未达）";
        out_ip.clear();
        out_isp.clear();
        out_threat = IpThreatInfo{};
        return false;
    }

    std::wstring w = Utf8ToWide(resp);
    std::wstring status = GetJsonStringValue(w, L"status");
    std::wstring message = GetJsonStringValue(w, L"message");
    std::wstring ip = GetJsonStringValue(w, L"query");
    if (ip.empty()) ip = GetJsonStringValue(w, L"ip");
    if (ip.empty()) ip = GetJsonStringValue(w, L"ip_address");
    if (ip.empty()) ip = GetJsonStringValue(w, L"origin");

    std::wstring country, region, city, isp;
    {
        std::wstring locObj = GetJsonObjectValue(w, L"location");
        if (!locObj.empty())
        {
            country = GetJsonStringValue(locObj, L"country");
            region  = GetJsonStringValue(locObj, L"regionName");
            if (region.empty()) region = GetJsonStringValue(locObj, L"region");
            if (region.empty()) region = GetJsonStringValue(locObj, L"state");
            city    = GetJsonStringValue(locObj, L"city");
        }
        else
        {
            std::vector<std::wstring> locArray = GetJsonArrayStrings(w, L"location");
            if (!locArray.empty())
            {
                if (locArray.size() > 0 && !locArray[0].empty()) country = locArray[0];
                if (locArray.size() > 1 && !locArray[1].empty()) region  = locArray[1];
                if (locArray.size() > 2 && !locArray[2].empty()) city    = locArray[2];
                if (locArray.size() > 4 && !locArray[4].empty())
                {
                    std::wstring rawIsp = locArray[4];
                    if (rawIsp.find(L"telecom") != std::wstring::npos) isp = L"中国电信";
                    else if (rawIsp.find(L"unicom") != std::wstring::npos) isp = L"中国联通";
                    else if (rawIsp.find(L"mobile") != std::wstring::npos) isp = L"中国移动";
                    else if (rawIsp.find(L"cernet") != std::wstring::npos) isp = L"教育网";
                    else if (rawIsp.find(L"cstnet") != std::wstring::npos) isp = L"科技网";
                    else isp = rawIsp;
                }
            }
        }
    }
    if (country.empty()) country = GetJsonStringValue(w, L"country");
    if (country.empty()) country = GetJsonStringValue(w, L"country_name");
    if (country.empty()) country = GetJsonStringValue(w, L"c");
    if (region.empty())  region  = GetJsonStringValue(w, L"regionName");
    if (region.empty())  region  = GetJsonStringValue(w, L"region");
    if (region.empty())  region  = GetJsonStringValue(w, L"province");
    if (region.empty())  region  = GetJsonStringValue(w, L"state");
    if (region.empty())  region  = GetJsonStringValue(w, L"pro");
    if (city.empty())    city    = GetJsonStringValue(w, L"city");

    if (isp.empty()) isp = GetJsonStringValue(w, L"isp");
    if (isp.empty()) isp = GetJsonStringValue(w, L"organization");
    if (isp.empty()) isp = GetJsonStringValue(w, L"name");
    if (isp.empty()) isp = GetJsonStringValue(w, L"org");
    if (isp.empty()) isp = GetJsonStringValue(w, L"operator");
    if (isp.empty())
    {
        std::wstring connObj = GetJsonObjectValue(w, L"connection");
        if (!connObj.empty())
        {
            isp = GetJsonStringValue(connObj, L"isp");
            if (isp.empty()) isp = GetJsonStringValue(connObj, L"org");
        }
    }

    out_threat = IpThreatInfo{};
    out_threat.is_proxy      = GetJsonBoolValue(w, L"is_proxy")      || GetJsonBoolValue(w, L"proxy");
    out_threat.is_vpn        = GetJsonBoolValue(w, L"is_vpn")        || GetJsonBoolValue(w, L"vpn");
    out_threat.is_datacenter = GetJsonBoolValue(w, L"is_datacenter") || GetJsonBoolValue(w, L"hosting");
    out_threat.is_tor        = GetJsonBoolValue(w, L"is_tor")        || GetJsonBoolValue(w, L"tor");
    out_threat.is_mobile     = GetJsonBoolValue(w, L"is_mobile")     || GetJsonBoolValue(w, L"mobile");
    out_threat.is_abuser     = GetJsonBoolValue(w, L"is_abuser")     || GetJsonBoolValue(w, L"abuser");

    {
        std::wstring asnObj = GetJsonObjectValue(w, L"asn");
        if (!asnObj.empty())
        {
            std::wstring asnType = GetJsonStringValue(asnObj, L"type");
            std::wstring t_lc   = asnType;
            for (auto& c : t_lc) c = towlower(c);

            if (t_lc == L"hosting" || t_lc == L"datacenter" || t_lc == L"cdn")
                out_threat.is_datacenter = true;
            else if (t_lc == L"mobile" || t_lc == L"wireless")
                out_threat.is_mobile = true;
        }
    }

    out_threat.risk_score = GetJsonStringValue(w, L"abuser_score");
    if (out_threat.risk_score.empty()) out_threat.risk_score = GetJsonStringValue(w, L"fraud_score");
    if (out_threat.risk_score.empty()) out_threat.risk_score = GetJsonStringValue(w, L"risk");

    if (status == L"fail" || status == L"error")
    {
        out_ip.clear(); out_place = message.empty() ? L"接口返回错误" : message; out_isp.clear();
        return false;
    }

    if (ip.empty())
    {
        out_place = L"解析失败（无效的数据格式）";
        out_ip.clear();
        out_isp.clear();
        return false;
    }

    out_ip = ip; out_isp = isp;
    std::wstring place;
    if (!country.empty()) place += country;
    if (!region.empty() && region != country)  { if (!place.empty()) place += L" "; place += region; }
    if (!city.empty() && city != region && city != country) { if (!place.empty()) place += L" "; place += city; }
    if (place.empty()) place = GetJsonStringValue(w, L"addr");
    if (place.empty()) place = GetJsonStringValue(w, L"address");
    out_place = place;
    return true;
}

bool CDataManager::FetchIpInfoWithFallback(const std::wstring& preferredUrl, bool use_proxy, std::wstring& out_place, std::wstring& out_ip, std::wstring& out_isp, IpThreatInfo& out_threat)
{
    std::vector<std::wstring> sources;
    if (!preferredUrl.empty())
    {
        sources.push_back(preferredUrl);
    }

    // 默认高可用源候选池
    const wchar_t* fallbackPool[] = {
        L"http://ip-api.com/json/?fields=status,message,country,regionName,city,isp,query,proxy,hosting&lang=zh-CN",
        L"https://api.ip.sb/geoip",
        L"https://ipwho.is/",
        L"https://ipinfo.io/json"
    };

    for (const auto* p : fallbackPool)
    {
        bool exists = false;
        for (const auto& s : sources)
        {
            if (s == p) { exists = true; break; }
        }
        if (!exists) sources.push_back(p);
    }

    for (size_t i = 0; i < sources.size(); ++i)
    {
        const auto& url = sources[i];
        std::wstring l_place, l_ip, l_isp;
        IpThreatInfo l_threat;
        if (FetchIpInfoByUrl(url, use_proxy, l_place, l_ip, l_isp, l_threat))
        {
            if (!l_ip.empty())
            {
                out_place = l_place;
                out_ip = l_ip;
                out_isp = l_isp;
                out_threat = l_threat;
                LogInfo(L"FetchIpInfo: [%ls] Succeeded via source #%d (%ls): IP=%ls, Place=%ls",
                    use_proxy ? L"Proxy" : L"Direct", (int)i + 1, url.c_str(), out_ip.c_str(), out_place.c_str());
                return true;
            }
        }
        else
        {
            LogInfo(L"FetchIpInfo: [%ls] Source #%d failed (%ls)", use_proxy ? L"Proxy" : L"Direct", (int)i + 1, url.c_str());
        }
    }

    LogInfo(L"FetchIpInfo: [%ls] All %d sources failed.", use_proxy ? L"Proxy" : L"Direct", (int)sources.size());
    out_place = L"请求失败（连接超时或网络未达）";
    out_ip.clear();
    out_isp.clear();
    out_threat = IpThreatInfo{};
    return false;
}
static void AppendWrappedList(std::wstring& out, const std::vector<std::pair<std::wstring, int>>& items,
                              const wchar_t* prefix, size_t perLine)
{
    std::wstring indent(prefix);
    std::wstring pad; pad.resize(indent.size(), L' ');

    size_t n = items.size(); size_t i = 0; bool firstLine = true;
    while (i < n)
    {
        if (!firstLine) { out += pad; } else { out += indent; }
        size_t cnt = 0;
        while (i < n && cnt < perLine)
        {
            if (cnt > 0) out += L" | ";
            out += items[i].first; out += L" - ";
            wchar_t b[32]{}; swprintf_s(b, L"%dms", items[i].second); out += b;
            ++i; ++cnt;
        }
        out += L"\n";
        firstLine = false;
    }
    if (n == 0)
    {
        out += indent; out += L"-\n";
    }
}

static std::wstring FormatItemTextHelper(bool is_proxy, const std::wstring& place, const std::wstring& ip, const std::wstring& isp, const std::vector<CDataManager::LatencyResult>& lats, int fmt);
void CDataManager::UpdateIpInfoNow()
{
    static std::atomic<bool> inside_update{ false };
    if (inside_update.exchange(true)) return;
    struct UpdateGuard {
        std::atomic<bool>& f;
        ~UpdateGuard() { f = false; }
    } guard{ inside_update };

    ULONGLONG start_tick = GetTickCount64();
    LogInfo(L"UpdateIpInfoNow: Background update thread started.");

    std::wstring l_public_ip_direct, l_place_direct, l_isp_direct;
    IpThreatInfo l_threat_direct;
    std::wstring l_public_ip_proxy, l_place_proxy, l_isp_proxy;
    IpThreatInfo l_threat_proxy;

    bool ok_direct = false;
    bool ok_proxy = false;
    std::vector<LatencyResult> l_latency;

    std::vector<LatencyTarget> targets = m_setting_data.latency_targets;
    if (targets.empty())
    {
        targets.push_back({ L"百度", L"https://www.baidu.com" });
        targets.push_back({ L"Google", L"https://www.google.com" });
    }

    bool use_proxy = m_setting_data.use_proxy;
    std::wstring proxy_addr = m_setting_data.proxy_address;

    // 1. 直连探测（高可用多源轮询）
    LogInfo(L"UpdateIpInfoNow: Fetching direct IP...");
    ok_direct = FetchIpInfoWithFallback(m_setting_data.api_domestic_url, false, l_place_direct, l_public_ip_direct, l_isp_direct, l_threat_direct);
    LogInfo(L"UpdateIpInfoNow: Direct query done (ok=%d).", ok_direct ? 1 : 0);

    // 2. 代理探测（高可用多源轮询）
    if (use_proxy)
    {
        LogInfo(L"UpdateIpInfoNow: Fetching proxy IP...");
        ok_proxy = FetchIpInfoWithFallback(m_setting_data.api_foreign_url, true, l_place_proxy, l_public_ip_proxy, l_isp_proxy, l_threat_proxy);
        LogInfo(L"UpdateIpInfoNow: Proxy query done (ok=%d).", ok_proxy ? 1 : 0);
    }

    // 3. 延迟测速
    LogInfo(L"UpdateIpInfoNow: Testing latencies for %d targets...", (int)targets.size());
    for (const auto& tgt : targets)
    {
        LogInfo(L"UpdateIpInfoNow: Testing target %ls...", tgt.name.c_str());
        LatencyResult r;
        r.name = tgt.name.empty() ? tgt.url : tgt.name;
        r.url  = tgt.url;
        try {
            MeasureLatencyMs(tgt.url, L"", true, r.direct_ms);
        } catch (...) {
            r.direct_ms = -1;
        }
        if (use_proxy)
        {
            try {
                MeasureLatencyMs(tgt.url, proxy_addr, false, r.proxy_ms);
            } catch (...) {
                r.proxy_ms = -1;
            }
        }
        l_latency.push_back(r);
    }
    LogInfo(L"UpdateIpInfoNow: Latency tests done.");

    // 平滑缓存保留：若偶发单次刷新失败且此前有成功记录，保留旧值，防止网络波动导致任务栏频繁红字闪烁
    if (ok_direct)
    {
        m_direct_fail_count = 0;
    }
    else
    {
        m_direct_fail_count++;
        if (!m_public_ip_direct.empty() && m_direct_fail_count <= 3)
        {
            LogInfo(L"UpdateIpInfoNow: Direct query failed (%d/3), retaining previous IP: %ls", m_direct_fail_count, m_public_ip_direct.c_str());
            l_public_ip_direct = m_public_ip_direct;
            l_place_direct     = m_place_direct;
            l_isp_direct       = m_isp_direct;
            l_threat_direct    = m_threat_direct;
            ok_direct = true;
        }
        else
        {
            l_place_direct = L"请求失败（连接超时或网络未达）";
            l_public_ip_direct.clear();
            l_isp_direct.clear();
            l_threat_direct = IpThreatInfo{};
        }
    }

    if (use_proxy)
    {
        if (ok_proxy)
        {
            m_proxy_fail_count = 0;
        }
        else
        {
            m_proxy_fail_count++;
            if (!m_public_ip_proxy.empty() && m_proxy_fail_count <= 3)
            {
                LogInfo(L"UpdateIpInfoNow: Proxy query failed (%d/3), retaining previous IP: %ls", m_proxy_fail_count, m_public_ip_proxy.c_str());
                l_public_ip_proxy = m_public_ip_proxy;
                l_place_proxy     = m_place_proxy;
                l_isp_proxy       = m_isp_proxy;
                l_threat_proxy    = m_threat_proxy;
                ok_proxy = true;
            }
            else
            {
                l_place_proxy = L"请求失败（连接超时或网络未达）";
                l_public_ip_proxy.clear();
                l_isp_proxy.clear();
                l_threat_proxy = IpThreatInfo{};
            }
        }
    }

    bool any_ok = ok_direct || ok_proxy;
    // 任务栏汇总文本
    std::wstring l_ip_region_display;
    if (!l_place_direct.empty()) { l_ip_region_display += L"直:"; l_ip_region_display += l_place_direct; }
    else if (!l_public_ip_direct.empty()) { l_ip_region_display += L"直:"; l_ip_region_display += l_public_ip_direct; }
    if (!l_place_proxy.empty()) { if (!l_ip_region_display.empty()) l_ip_region_display += L" | "; l_ip_region_display += L"代:"; l_ip_region_display += l_place_proxy; }
    else if (!l_public_ip_proxy.empty()) { if (!l_ip_region_display.empty()) l_ip_region_display += L" | "; l_ip_region_display += L"代:"; l_ip_region_display += l_public_ip_proxy; }

    // 构造 Tooltip
    auto FormatThreatStr = [](const IpThreatInfo& t) -> std::wstring {
        std::wstring type;
        if (t.is_datacenter) { if (!type.empty()) type += L" + "; type += L"IDC机房"; }
        if (t.is_mobile)     { if (!type.empty()) type += L" + "; type += L"移动网络"; }
        if (t.is_vpn)        { if (!type.empty()) type += L" + "; type += L"VPN"; }
        if (t.is_proxy)      { if (!type.empty()) type += L" + "; type += L"代理"; }
        if (t.is_tor)        { if (!type.empty()) type += L" + "; type += L"Tor"; }
        if (type.empty()) type = L"普通宽带";

        std::wstring status = t.is_abuser ? L"恶意/黑名单" : L"纯净";
        if (!t.risk_score.empty()) { status += L" (欺诈分: " + t.risk_score + L")"; }
        return L"    类型: " + type + L" | 状态: " + status + L"\n";
    };

    std::wstring l_tooltip;
    if (any_ok)
    {
        SYSTEMTIME st{}; GetLocalTime(&st);
        wchar_t tb[64]{}; swprintf_s(tb, L"%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        
        if (!l_public_ip_direct.empty())
        {
            l_tooltip += L"直: " + l_public_ip_direct;
            if (!l_place_direct.empty()) l_tooltip += L"  [" + l_place_direct + L"]";
            if (!l_isp_direct.empty()) l_tooltip += L"  " + l_isp_direct;
            l_tooltip += L"\n" + FormatThreatStr(l_threat_direct);
        }
        if (!l_public_ip_proxy.empty())
        {
            l_tooltip += L"代: " + l_public_ip_proxy;
            if (!l_place_proxy.empty()) l_tooltip += L"  [" + l_place_proxy + L"]";
            if (!l_isp_proxy.empty()) l_tooltip += L"  " + l_isp_proxy;
            l_tooltip += L"\n" + FormatThreatStr(l_threat_proxy);
        }
        if (!m_setting_data.use_proxy) l_tooltip += L"（未启用代理）\n";

        if (!l_latency.empty())
        {
            std::vector<std::pair<std::wstring,int>> ds, ps;
            for (const auto& it : l_latency)
            {
                if (it.direct_ms >= 0) ds.emplace_back(it.name, (int)(it.direct_ms + 0.5));
                if (it.proxy_ms  >= 0) ps.emplace_back(it.name, (int)(it.proxy_ms  + 0.5));
            }
            l_tooltip += L"延迟:\n";
            AppendWrappedList(l_tooltip, ds, L"直: ", 3);
            if (m_setting_data.use_proxy) AppendWrappedList(l_tooltip, ps, L"代: ", 3);
            else l_tooltip += L"代: -\n";
        }
        l_tooltip += L"最后更新: "; l_tooltip += tb;
    }

    m_last_ip_update_tick = GetTickCount64();

    {
        std::lock_guard<std::mutex> lock(m_swap_mutex);
        m_public_ip_direct_bg = l_public_ip_direct;
        m_place_direct_bg = l_place_direct;
        m_isp_direct_bg = l_isp_direct;
        m_threat_direct_bg = l_threat_direct;

        m_public_ip_proxy_bg = l_public_ip_proxy;
        m_place_proxy_bg = l_place_proxy;
        m_isp_proxy_bg = l_isp_proxy;
        m_threat_proxy_bg = l_threat_proxy;

        m_latency_results_bg = l_latency;
        m_ip_region_display_bg = l_ip_region_display;
        m_tooltip_bg = l_tooltip;
        m_direct_item_text_bg = FormatItemTextHelper(false, l_place_direct, l_public_ip_direct, l_isp_direct, l_latency, m_setting_data.display_format);
        m_proxy_item_text_bg  = FormatItemTextHelper(true,  l_place_proxy,  l_public_ip_proxy,  l_isp_proxy,  l_latency, m_setting_data.display_format);

        m_has_new_data = true;
    }

    LogInfo(L"UpdateIpInfoNow: Background update completed in %.2f s.", (GetTickCount64() - start_tick) / 1000.0);
    inside_update = false;
}

static std::wstring FormatItemTextHelper(bool is_proxy, const std::wstring& place, const std::wstring& ip, const std::wstring& isp, const std::vector<CDataManager::LatencyResult>& lats, int fmt)
{
    switch (fmt)
    {
    case DF_REGION_ONLY:
        if (!place.empty()) return place;
        return ip.empty() ? L"..." : ip;

    case DF_IP_ONLY:
        if (!ip.empty()) return ip;
        return place.empty() ? L"..." : place;

    case DF_REGION_AND_LATENCY:
    {
        std::wstring out = place.empty() ? ip : place;
        for (const auto& r : lats)
        {
            double ms = is_proxy ? r.proxy_ms : r.direct_ms;
            if (ms >= 0)
            {
                wchar_t b[32]{}; swprintf_s(b, L" %dms", (int)(ms + 0.5));
                out += b;
                break;
            }
        }
        return out.empty() ? L"..." : out;
    }

    case DF_FULL:
    {
        std::wstring out = place;
        if (!isp.empty()) { if (!out.empty()) out += L" "; out += isp; }
        if (!ip.empty())  { out += L" (" + ip + L")"; }
        return out.empty() ? L"..." : out;
    }

    case DF_REGION_AND_IP:
    default:
    {
        if (!place.empty() && !ip.empty())
            return place + L" (" + ip + L")";
        if (!place.empty()) return place;
        if (!ip.empty())    return ip;
        return L"...";
    }
    }
}

std::wstring CDataManager::FormatItemText(bool is_proxy) const
{
    const std::wstring& s = is_proxy ? m_proxy_item_text : m_direct_item_text;
    if (!s.empty()) return s;
    return FormatItemTextHelper(is_proxy, is_proxy ? m_place_proxy : m_place_direct, is_proxy ? m_public_ip_proxy : m_public_ip_direct, is_proxy ? m_isp_proxy : m_isp_direct, m_latency_results, m_setting_data.display_format);
}

const wchar_t* CDataManager::GetFormattedItemText(bool is_proxy) const
{
    const std::wstring& s = is_proxy ? m_proxy_item_text : m_direct_item_text;
    return s.empty() ? L"..." : s.c_str();
}

bool CDataManager::IsUpdating() const
{
    return m_is_updating;
}

void CDataManager::_ThreadProc()
{
    UpdateIpInfoNow();
    m_is_updating = false;
}

void CDataManager::ReformatTaskbarTexts()
{
    std::lock_guard<std::mutex> lock(m_swap_mutex);
    m_direct_item_text = FormatItemTextHelper(false, m_place_direct, m_public_ip_direct, m_isp_direct, m_latency_results, m_setting_data.display_format);
    m_proxy_item_text  = FormatItemTextHelper(true,  m_place_proxy,  m_public_ip_proxy,  m_isp_proxy,  m_latency_results, m_setting_data.display_format);
    m_direct_item_text_bg = m_direct_item_text;
    m_proxy_item_text_bg  = m_proxy_item_text;
}

int CDataManager::GetRefreshCountdownSec() const
{
    if (m_is_updating) return 0;
    if (m_last_ip_update_tick == 0) return 0;
    ULONGLONG now = GetTickCount64();
    ULONGLONG interval = (ULONGLONG)m_setting_data.refresh_interval_sec * 1000ULL;
    if (now >= m_last_ip_update_tick + interval) return 0;
    ULONGLONG remain = (m_last_ip_update_tick + interval) - now;
    return (int)((remain + 999ULL) / 1000ULL);
}

void CDataManager::UpdateIpInfoIfNeeded()
{
    m_ip_update_interval_ms = (unsigned int)m_setting_data.refresh_interval_sec * 1000u;
    ULONGLONG now = GetTickCount64();
    if (m_last_ip_update_tick != 0 && (now - m_last_ip_update_tick) < m_ip_update_interval_ms)
        return;

    ForceUpdateAsync();
}

void CDataManager::ForceUpdateAsync()
{
    if (m_is_updating) return;

    LogInfo(L"ForceUpdateAsync: Spawning background update thread.");
    m_is_updating = true;
    std::thread([this]() {
        try {
            UpdateIpInfoNow();
        } catch (...) {
            LogInfo(L"ForceUpdateAsync: Unhandled exception caught in update thread.");
        }
        m_is_updating = false;
    }).detach();
}

std::wstring CDataManager::GetLastUpdateStatusString() const
{
    const SYSTEMTIME& st = m_last_update_time; if (st.wYear == 0) return L"";
    wchar_t tb[64]{}; swprintf_s(tb, L"%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    std::wstring s = L"最后更新: "; s += tb; return s;
}

void CDataManager::SwapBuffers()
{
    if (!m_has_new_data) return;

    std::lock_guard<std::mutex> lock(m_swap_mutex);

    m_public_ip_direct = m_public_ip_direct_bg;
    m_public_ip_proxy = m_public_ip_proxy_bg;
    m_place_direct = m_place_direct_bg;
    m_place_proxy = m_place_proxy_bg;
    m_isp_direct = m_isp_direct_bg;
    m_isp_proxy = m_isp_proxy_bg;
    m_latency_results = m_latency_results_bg;
    m_tooltip = m_tooltip_bg;
    m_ip_region_display = m_ip_region_display_bg;
    m_threat_direct = m_threat_direct_bg;
    m_threat_proxy = m_threat_proxy_bg;
    m_direct_item_text = m_direct_item_text_bg;
    m_proxy_item_text = m_proxy_item_text_bg;

    m_has_new_data = false;
}

void CDataManager::RotateLogIfNeeded(const std::wstring& log_path)
{
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExW(log_path.c_str(), GetFileExInfoStandard, &fad))
    {
        LARGE_INTEGER size;
        size.LowPart = fad.nFileSizeLow;
        size.HighPart = fad.nFileSizeHigh;
        if (size.QuadPart > 1024 * 1024) // 超过 1MB 自动滚动
        {
            std::wstring bak_path = log_path + L".bak";
            DeleteFileW(bak_path.c_str());
            MoveFileExW(log_path.c_str(), bak_path.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
    }
}

void CDataManager::LogInfo(const wchar_t* format, ...)
{
    if (m_config_path.empty()) return;

    std::wstring log_path = m_config_path;
    size_t dot_pos = log_path.find_last_of(L".");
    if (dot_pos != std::wstring::npos)
        log_path = log_path.substr(0, dot_pos);
    log_path += L"_debug.log";

    RotateLogIfNeeded(log_path);

    va_list args;
    va_start(args, format);
    int len = _vscwprintf(format, args) + 1;
    std::vector<wchar_t> buf(len);
    vswprintf_s(buf.data(), len, format, args);
    va_end(args);

    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t time_str[64]{};
    swprintf_s(time_str, L"[%04d-%02d-%02d %02d:%02d:%02d.%03d] ",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    std::wstring full_msg = time_str + std::wstring(buf.data()) + L"\n";
    std::string utf8_msg = WideToUtf8(full_msg);

    std::ofstream log_file(log_path, std::ios::out | std::ios::app | std::ios::binary);
    if (log_file.is_open())
    {
        log_file.write(utf8_msg.data(), utf8_msg.size());
        log_file.flush();
    }
}
