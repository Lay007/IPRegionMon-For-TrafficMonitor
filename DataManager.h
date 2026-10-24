#pragma once
#include <string>
#include <map>
#include <vector>
#include <Windows.h>
#include <atomic>
#include <mutex>

#define g_data CDataManager::Instance()

struct IpThreatInfo {
    bool is_proxy{false};
    bool is_vpn{false};
    bool is_datacenter{false}; // Hosting / Datacenter
    bool is_tor{false};
    bool is_mobile{false};     // Cellular
    bool is_abuser{false};     // 恶意/黑名单
    std::wstring risk_score;   // 欺诈分/风险分
};

struct LatencyTarget
{
    std::wstring name;   // 展示名称
    std::wstring url;    // 测试地址（域名或URL）
};

enum DisplayFormat
{
    DF_REGION_ONLY = 0,        // 仅地区 (例如: CN 上海)
    DF_REGION_AND_IP = 1,      // 地区 + IP (例如: CN 上海 (1.2.3.4))
    DF_IP_ONLY = 2,            // 仅 IP (例如: 1.2.3.4)
    DF_REGION_AND_LATENCY = 3, // 地区 + 延迟 (例如: CN 上海 15ms)
    DF_FULL = 4                // 完整 (例如: CN 上海 电信 (1.2.3.4))
};

struct SettingData
{
    bool show_second{};
    // 是否使用代理访问（用于国外API）
    bool use_proxy{};
    // 代理地址，例如：127.0.0.1:7890 （HTTP 代理）
    std::wstring proxy_address;
    // 国内与国外 API 地址
    std::wstring api_domestic_url;   // 国内线路 API
    std::wstring api_foreign_url;    // 国外线路 API
    // 兼容旧版（单一 URL），若不为空作为两个 URL 的默认值
    std::wstring api_url;
    // 刷新间隔（秒）
    int refresh_interval_sec{ 300 }; // 默认 5 分钟
    // 是否在数值文本中显示IP（兼容旧版设置）
    bool show_ip{ true };
    // 显示格式 (0: 仅地区, 1: 地区+IP, 2: 仅IP, 3: 地区+延迟, 4: 完整信息)
    int display_format{ DF_REGION_AND_IP };
    // 延迟目标（动态列表）
    std::vector<LatencyTarget> latency_targets;
};

class CDataManager
{
    friend class CIPRegionMon;

private:
    CDataManager();
    ~CDataManager();

public:
    static CDataManager& Instance();

    void LoadConfig(const std::wstring& config_dir);
    void SaveConfig() const;
    const wchar_t* StringRes(UINT id);      //根据资源id获取一个字符串资源

    void UpdateIpInfoNow();         // 立即获取信息并测速（并发异步执行，勿在UI主线程调用）
    void UpdateIpInfoIfNeeded();    // 节流定时获取，适合在计时器中调用
    void ForceUpdateAsync();        // 强制启动后台异步刷新（供UI按钮调用）
    bool IsUpdating() const;        // 是否正在后台刷新
    void _ThreadProc();             // 内部供线程调用
    std::wstring GetLastUpdateStatusString() const;
    void SwapBuffers();             // 将后台缓冲数据同步到主线程使用

    // 格式化输出任务栏监控文本
    std::wstring FormatItemText(bool is_proxy) const;

    // 单独测试代理服务器连通性（同步执行，应在后台调用）
    bool TestProxyConnectivity(const std::wstring& proxy, std::wstring& out_message, double& out_ms);

    // 单独测试指定 URL 的延迟（同步执行，ms）
    static bool MeasureLatencyMs(const std::wstring& url_or_host, const std::wstring& proxy, bool no_proxy, double& out_ms);

    // 写入日志到 IPRegionMon_debug.log（带自动大小滚动控制）
    void LogInfo(const wchar_t* format, ...);

public:
    std::wstring m_cur_time;
    std::wstring m_cur_date;
    SYSTEMTIME m_system_time;
    SettingData m_setting_data;

    // IP 与地区显示
    std::wstring m_public_ip;            // 历史字段（兼容）
    std::wstring m_ip_region_display;    // 任务栏显示文本

    // 双IP
    std::wstring m_public_ip_direct;     // 直连（不经代理）公网IP
    std::wstring m_public_ip_proxy;      // 代理出口公网IP
    std::wstring m_place_direct;         // 直连的地区字符串（国家 省/州 城市）
    std::wstring m_place_proxy;          // 代理的地区字符串
    std::wstring m_isp_direct;           // 直连 ISP
    std::wstring m_isp_proxy;            // 代理 ISP
    IpThreatInfo m_threat_direct;        // 直连威胁/泄漏信息
    IpThreatInfo m_threat_proxy;         // 代理威胁/泄漏信息

    // 延迟（毫秒，<0 表示无效），按用户配置存放结果
    struct LatencyResult { std::wstring name; std::wstring url; double direct_ms{-1}; double proxy_ms{-1}; };
    std::vector<LatencyResult> m_latency_results;

    // 鼠标提示（完整地区信息）
    std::wstring m_tooltip;

private:
    static CDataManager m_instance;
    std::wstring m_config_path;
    std::map<UINT, std::wstring> m_string_table;

    // IP 更新节流
    ULONGLONG m_last_ip_update_tick{0};
    unsigned int m_ip_update_interval_ms{ 5 * 60 * 1000 }; // 默认5分钟

    // 最后更新信息
    bool m_last_source_domestic{ false };
    SYSTEMTIME m_last_update_time{};      // 本地时间

    // 执行一次请求，返回 place、ip、isp、threat
    bool FetchIpInfoByUrl(const std::wstring& url, bool use_proxy, std::wstring& out_place, std::wstring& out_ip, std::wstring& out_isp, IpThreatInfo& out_threat);

    // 日志滚动管理
    void RotateLogIfNeeded(const std::wstring& log_path);

    // 后台缓存变量 (Double Buffering)
    std::wstring m_public_ip_direct_bg;
    std::wstring m_public_ip_proxy_bg;
    std::wstring m_place_direct_bg;
    std::wstring m_place_proxy_bg;
    std::wstring m_isp_direct_bg;
    std::wstring m_isp_proxy_bg;
    IpThreatInfo m_threat_direct_bg;
    IpThreatInfo m_threat_proxy_bg;
    std::vector<LatencyResult> m_latency_results_bg;
    std::wstring m_tooltip_bg;
    std::wstring m_ip_region_display_bg;

    std::atomic<bool> m_is_updating{ false };
    std::atomic<bool> m_has_new_data{ false };
    std::mutex m_swap_mutex;
};
