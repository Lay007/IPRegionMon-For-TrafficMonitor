#include "pch.h"
#include "OptionsDlg.h"
#include <thread>

// ====================================================================
// CTabProxyApiDlg —— Tab 0: 常规 / 网络 & 代理
// ====================================================================
IMPLEMENT_DYNAMIC(CTabProxyApiDlg, CDialog)

CTabProxyApiDlg::CTabProxyApiDlg(CWnd* pParent)
    : CDialog(IDD_TAB_PROXY_API, pParent) {}

void CTabProxyApiDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_DISPLAY_FORMAT_COMBO,   m_cmbDisplayFormat);
    DDX_Control(pDX, IDC_USE_PROXY_CHECK,        m_chkUseProxy);
    DDX_Control(pDX, IDC_PROXY_EDIT,             m_edtProxy);
    DDX_Control(pDX, IDC_API_DOMESTIC_EDIT,      m_edtDomesticApi);
    DDX_Control(pDX, IDC_API_FOREIGN_EDIT,       m_edtForeignApi);
    DDX_Control(pDX, IDC_REFRESH_INTERVAL_EDIT,  m_edtInterval);
    DDX_Control(pDX, IDC_REFRESH_NOW_BUTTON,     m_btnRefresh);
    DDX_Control(pDX, IDC_STATUS_STATIC,          m_stcStatus);
    DDX_Control(pDX, IDC_TEST_PROXY_BUTTON,      m_btnTestProxy);
    DDX_Control(pDX, IDC_TEST_PROXY_STATUS,      m_stcTestProxyStatus);
}

BEGIN_MESSAGE_MAP(CTabProxyApiDlg, CDialog)
    ON_BN_CLICKED(IDC_USE_PROXY_CHECK,    &CTabProxyApiDlg::OnBnClickedUseProxy)
    ON_BN_CLICKED(IDC_REFRESH_NOW_BUTTON, &CTabProxyApiDlg::OnBnClickedRefreshNow)
    ON_BN_CLICKED(IDC_TEST_PROXY_BUTTON,  &CTabProxyApiDlg::OnBnClickedTestProxy)
    ON_WM_TIMER()
END_MESSAGE_MAP()

BOOL CTabProxyApiDlg::OnInitDialog()
{
    CDialog::OnInitDialog();

    // 初始化显示格式下拉框
    m_cmbDisplayFormat.ResetContent();
    m_cmbDisplayFormat.AddString(L"仅地区 (例如: CN 上海)");
    m_cmbDisplayFormat.AddString(L"地区 + IP (例如: CN 上海 (1.2.3.4))");
    m_cmbDisplayFormat.AddString(L"仅 IP (例如: 1.2.3.4)");
    m_cmbDisplayFormat.AddString(L"地区 + 延迟 (例如: CN 上海 15ms)");
    m_cmbDisplayFormat.AddString(L"完整信息 (例如: CN 上海 电信 (1.2.3.4))");

    const SettingData& d = g_data.m_setting_data;
    int fmt = d.display_format;
    if (fmt < 0 || fmt > 4) fmt = 1;
    m_cmbDisplayFormat.SetCurSel(fmt);

    m_chkUseProxy.SetCheck(d.use_proxy ? BST_CHECKED : BST_UNCHECKED);
    m_edtProxy.SetWindowTextW(d.proxy_address.c_str());
    m_edtDomesticApi.SetWindowTextW(d.api_domestic_url.c_str());
    m_edtForeignApi.SetWindowTextW(d.api_foreign_url.c_str());

    wchar_t buf[16]{};
    swprintf_s(buf, L"%d", d.refresh_interval_sec);
    m_edtInterval.SetWindowTextW(buf);

    UpdateProxyEditState();
    return TRUE;
}

void CTabProxyApiDlg::UpdateProxyEditState()
{
    BOOL useProxy = (m_chkUseProxy.GetCheck() == BST_CHECKED);
    GetDlgItem(IDC_PROXY_LABEL)->EnableWindow(useProxy);
    m_edtProxy.EnableWindow(useProxy);
    m_btnTestProxy.EnableWindow(useProxy);
    GetDlgItem(IDC_API_FOREIGN_LABEL)->EnableWindow(useProxy);
    m_edtForeignApi.EnableWindow(useProxy);
}

void CTabProxyApiDlg::OnBnClickedUseProxy()
{
    UpdateProxyEditState();
}

void CTabProxyApiDlg::OnBnClickedRefreshNow()
{
    g_data.ForceUpdateAsync();
    m_btnRefresh.EnableWindow(FALSE);
    m_stcStatus.SetWindowTextW(L"正在并发探测各线路与测速目标…");
    SetTimer(1, 300, nullptr);
}

void CTabProxyApiDlg::OnBnClickedTestProxy()
{
    CString proxy;
    m_edtProxy.GetWindowTextW(proxy);
    std::wstring proxy_str = proxy.GetString();

    m_btnTestProxy.EnableWindow(FALSE);
    m_stcTestProxyStatus.SetWindowTextW(L"正在测试代理服务器连通性…");

    HWND hWnd = m_hWnd;
    std::thread([hWnd, proxy_str]() {
        std::wstring msg;
        double ms = -1.0;
        bool ok = g_data.TestProxyConnectivity(proxy_str, msg, ms);
        if (::IsWindow(hWnd))
        {
            ::SetDlgItemTextW(hWnd, IDC_TEST_PROXY_STATUS, msg.c_str());
            ::EnableWindow(::GetDlgItem(hWnd, IDC_TEST_PROXY_BUTTON), TRUE);
        }
    }).detach();
}

void CTabProxyApiDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == 1 && !g_data.IsUpdating())
    {
        KillTimer(1);
        m_btnRefresh.EnableWindow(TRUE);
        m_stcStatus.SetWindowTextW(L"刷新完成！");
    }
    CDialog::OnTimer(nIDEvent);
}

void CTabProxyApiDlg::GetData(SettingData& out) const
{
    out.display_format = m_cmbDisplayFormat.GetCurSel();
    if (out.display_format < 0) out.display_format = DF_REGION_AND_IP;
    out.show_ip = (out.display_format == DF_REGION_AND_IP || out.display_format == DF_IP_ONLY || out.display_format == DF_FULL);

    out.use_proxy = (m_chkUseProxy.GetCheck() == BST_CHECKED);

    CString s;
    m_edtProxy.GetWindowTextW(s);       out.proxy_address = s.GetString();
    m_edtDomesticApi.GetWindowTextW(s); out.api_domestic_url = s.GetString();
    m_edtForeignApi.GetWindowTextW(s);  out.api_foreign_url  = s.GetString();

    m_edtInterval.GetWindowTextW(s);
    int secs = _wtoi(s.GetString());
    out.refresh_interval_sec = (secs >= 5) ? secs : 5;
}

// ====================================================================
// CTabLatencyDlg —— Tab 1: 延迟目标动态列表
// ====================================================================
IMPLEMENT_DYNAMIC(CTabLatencyDlg, CDialog)

CTabLatencyDlg::CTabLatencyDlg(CWnd* pParent)
    : CDialog(IDD_TAB_LATENCY_LIST, pParent) {}

void CTabLatencyDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_LATENCY_LIST,  m_listCtrl);
    DDX_Control(pDX, IDC_LAT_NAME_EDIT, m_edtName);
    DDX_Control(pDX, IDC_LAT_URL_EDIT,  m_edtUrl);
}

BEGIN_MESSAGE_MAP(CTabLatencyDlg, CDialog)
    ON_BN_CLICKED(IDC_LAT_ADD_BUTTON,  &CTabLatencyDlg::OnBnClickedAdd)
    ON_BN_CLICKED(IDC_LAT_DEL_BUTTON,  &CTabLatencyDlg::OnBnClickedDel)
    ON_BN_CLICKED(IDC_LAT_UP_BUTTON,   &CTabLatencyDlg::OnBnClickedUp)
    ON_BN_CLICKED(IDC_LAT_DOWN_BUTTON, &CTabLatencyDlg::OnBnClickedDown)
    ON_BN_CLICKED(IDC_LAT_TEST_BUTTON, &CTabLatencyDlg::OnBnClickedTest)
    ON_NOTIFY(NM_CLICK,   IDC_LATENCY_LIST, &CTabLatencyDlg::OnNMClickList)
    ON_NOTIFY(NM_DBLCLK,  IDC_LATENCY_LIST, &CTabLatencyDlg::OnNMDblclkList)
END_MESSAGE_MAP()

BOOL CTabLatencyDlg::OnInitDialog()
{
    CDialog::OnInitDialog();

    m_listCtrl.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    m_listCtrl.InsertColumn(0, L"#",     LVCFMT_LEFT, 35);
    m_listCtrl.InsertColumn(1, L"名称",  LVCFMT_LEFT, 85);
    m_listCtrl.InsertColumn(2, L"测试地址 (URL / Host)", LVCFMT_LEFT, 185);
    m_listCtrl.InsertColumn(3, L"最新延迟 (直 / 代)",    LVCFMT_LEFT, 110);

    m_targets = g_data.m_setting_data.latency_targets;
    RefreshList();

    return TRUE;
}

void CTabLatencyDlg::RefreshList()
{
    m_listCtrl.DeleteAllItems();

    const auto& results = g_data.m_latency_results;

    for (size_t i = 0; i < m_targets.size(); ++i)
    {
        wchar_t numStr[16]{};
        swprintf_s(numStr, L"%d", (int)i + 1);
        int idx = m_listCtrl.InsertItem((int)i, numStr);
        m_listCtrl.SetItemText(idx, 1, m_targets[i].name.c_str());
        m_listCtrl.SetItemText(idx, 2, m_targets[i].url.c_str());

        // 匹配延迟结果
        std::wstring lat_str = L"-";
        for (const auto& r : results)
        {
            if (r.url == m_targets[i].url || r.name == m_targets[i].name)
            {
                wchar_t b[64]{};
                std::wstring d = (r.direct_ms >= 0) ? std::to_wstring((int)(r.direct_ms + 0.5)) + L"ms" : L"-";
                std::wstring p = (r.proxy_ms  >= 0) ? std::to_wstring((int)(r.proxy_ms  + 0.5)) + L"ms" : L"-";
                swprintf_s(b, L"%s / %s", d.c_str(), p.c_str());
                lat_str = b;
                break;
            }
        }
        m_listCtrl.SetItemText(idx, 3, lat_str.c_str());
    }
}

void CTabLatencyDlg::LoadSelectedToEdit()
{
    POSITION pos = m_listCtrl.GetFirstSelectedItemPosition();
    if (pos)
    {
        int sel = m_listCtrl.GetNextSelectedItem(pos);
        if (sel >= 0 && sel < (int)m_targets.size())
        {
            m_edtName.SetWindowTextW(m_targets[sel].name.c_str());
            m_edtUrl.SetWindowTextW(m_targets[sel].url.c_str());
        }
    }
}

void CTabLatencyDlg::OnNMClickList(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
    LoadSelectedToEdit();
    *pResult = 0;
}

void CTabLatencyDlg::OnNMDblclkList(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
    LoadSelectedToEdit();
    *pResult = 0;
}

void CTabLatencyDlg::OnBnClickedAdd()
{
    CString name, url;
    m_edtName.GetWindowTextW(name);
    m_edtUrl.GetWindowTextW(url);

    std::wstring u = url.GetString();
    if (u.empty())
    {
        AfxMessageBox(L"请输入有效的测试地址或域名！", MB_ICONWARNING);
        return;
    }
    std::wstring n = name.GetString();
    if (n.empty()) n = u;

    // 检查是否存在当前选中项进行更新
    POSITION pos = m_listCtrl.GetFirstSelectedItemPosition();
    if (pos)
    {
        int sel = m_listCtrl.GetNextSelectedItem(pos);
        if (sel >= 0 && sel < (int)m_targets.size())
        {
            m_targets[sel].name = n;
            m_targets[sel].url  = u;
            RefreshList();
            m_listCtrl.SetItemState(sel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            return;
        }
    }

    // 否则添加新目标
    m_targets.push_back({ n, u });
    RefreshList();
    m_edtName.SetWindowTextW(L"");
    m_edtUrl.SetWindowTextW(L"");
}

void CTabLatencyDlg::OnBnClickedDel()
{
    POSITION pos = m_listCtrl.GetFirstSelectedItemPosition();
    if (pos)
    {
        int sel = m_listCtrl.GetNextSelectedItem(pos);
        if (sel >= 0 && sel < (int)m_targets.size())
        {
            m_targets.erase(m_targets.begin() + sel);
            RefreshList();
            m_edtName.SetWindowTextW(L"");
            m_edtUrl.SetWindowTextW(L"");
        }
    }
}

void CTabLatencyDlg::OnBnClickedUp()
{
    POSITION pos = m_listCtrl.GetFirstSelectedItemPosition();
    if (pos)
    {
        int sel = m_listCtrl.GetNextSelectedItem(pos);
        if (sel > 0 && sel < (int)m_targets.size())
        {
            std::swap(m_targets[sel], m_targets[sel - 1]);
            RefreshList();
            m_listCtrl.SetItemState(sel - 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        }
    }
}

void CTabLatencyDlg::OnBnClickedDown()
{
    POSITION pos = m_listCtrl.GetFirstSelectedItemPosition();
    if (pos)
    {
        int sel = m_listCtrl.GetNextSelectedItem(pos);
        if (sel >= 0 && sel + 1 < (int)m_targets.size())
        {
            std::swap(m_targets[sel], m_targets[sel + 1]);
            RefreshList();
            m_listCtrl.SetItemState(sel + 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        }
    }
}

void CTabLatencyDlg::OnBnClickedTest()
{
    POSITION pos = m_listCtrl.GetFirstSelectedItemPosition();
    if (!pos)
    {
        AfxMessageBox(L"请先在列表中选中需要测速的目标行！", MB_ICONINFORMATION);
        return;
    }
    int sel = m_listCtrl.GetNextSelectedItem(pos);
    if (sel < 0 || sel >= (int)m_targets.size()) return;

    LatencyTarget tgt = m_targets[sel];
    bool use_proxy = g_data.m_setting_data.use_proxy;
    std::wstring proxy = g_data.m_setting_data.proxy_address;
    HWND hWnd = m_hWnd;

    m_listCtrl.SetItemText(sel, 3, L"测速中…");

    std::thread([hWnd, sel, tgt, use_proxy, proxy]() {
        double d_ms = -1, p_ms = -1;
        CDataManager::MeasureLatencyMs(tgt.url, L"", true, d_ms);
        if (use_proxy)
            CDataManager::MeasureLatencyMs(tgt.url, proxy, false, p_ms);

        if (::IsWindow(hWnd))
        {
            wchar_t b[64]{};
            std::wstring d = (d_ms >= 0) ? std::to_wstring((int)(d_ms + 0.5)) + L"ms" : L"-";
            std::wstring p = (p_ms >= 0) ? std::to_wstring((int)(p_ms + 0.5)) + L"ms" : L"-";
            swprintf_s(b, L"%s / %s", d.c_str(), p.c_str());
            HWND hList = ::GetDlgItem(hWnd, IDC_LATENCY_LIST);
            if (hList)
            {
                LVITEM item{};
                item.iSubItem = 3;
                item.pszText = b;
                ::SendMessage(hList, LVM_SETITEMTEXT, (WPARAM)sel, (LPARAM)&item);
            }
        }
    }).detach();
}

void CTabLatencyDlg::GetData(SettingData& out) const
{
    out.latency_targets = m_targets;
}

// ====================================================================
// CTabStatusDlg —— Tab 2: 当前状态与安全检测
// ====================================================================
IMPLEMENT_DYNAMIC(CTabStatusDlg, CDialog)

CTabStatusDlg::CTabStatusDlg(CWnd* pParent)
    : CDialog(IDD_TAB_STATUS_INFO, pParent) {}

void CTabStatusDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_STAT_DIRECT_IP,     m_stcDirectIp);
    DDX_Control(pDX, IDC_STAT_DIRECT_PLACE,  m_stcDirectPlace);
    DDX_Control(pDX, IDC_STAT_DIRECT_ISP,    m_stcDirectIsp);
    DDX_Control(pDX, IDC_STAT_DIRECT_THREAT, m_stcDirectThreat);

    DDX_Control(pDX, IDC_STAT_PROXY_IP,      m_stcProxyIp);
    DDX_Control(pDX, IDC_STAT_PROXY_PLACE,   m_stcProxyPlace);
    DDX_Control(pDX, IDC_STAT_PROXY_ISP,     m_stcProxyIsp);
    DDX_Control(pDX, IDC_STAT_PROXY_THREAT,  m_stcProxyThreat);

    DDX_Control(pDX, IDC_STATUS_INFO_EDIT,   m_edtStatus);
}

BEGIN_MESSAGE_MAP(CTabStatusDlg, CDialog)
    ON_BN_CLICKED(IDC_BTN_COPY_DIRECT, &CTabStatusDlg::OnBnClickedCopyDirect)
    ON_BN_CLICKED(IDC_BTN_COPY_PROXY,  &CTabStatusDlg::OnBnClickedCopyProxy)
    ON_BN_CLICKED(IDC_BTN_COPY_ALL,    &CTabStatusDlg::OnBnClickedCopyAll)
END_MESSAGE_MAP()

BOOL CTabStatusDlg::OnInitDialog()
{
    CDialog::OnInitDialog();
    UpdateStatusView();
    return TRUE;
}

void CTabStatusDlg::UpdateStatusView()
{
    auto FormatThreatBadge = [](const IpThreatInfo& t) -> std::wstring {
        std::wstring type;
        if (t.is_datacenter) { if (!type.empty()) type += L" + "; type += L"IDC机房"; }
        if (t.is_mobile)     { if (!type.empty()) type += L" + "; type += L"移动网络"; }
        if (t.is_vpn)        { if (!type.empty()) type += L" + "; type += L"VPN"; }
        if (t.is_proxy)      { if (!type.empty()) type += L" + "; type += L"代理"; }
        if (t.is_tor)        { if (!type.empty()) type += L" + "; type += L"Tor"; }
        if (type.empty()) type = L"普通住宅宽带";

        std::wstring out = L"网络类型: " + type + L"\n纯净度: " + (t.is_abuser ? L"恶意黑名单" : L"纯净");
        if (!t.risk_score.empty()) out += L" (欺诈分: " + t.risk_score + L")";
        return out;
    };

    // 直连卡片
    m_stcDirectIp.SetWindowTextW((L"公网 IP: " + (g_data.m_public_ip_direct.empty() ? L"-" : g_data.m_public_ip_direct)).c_str());
    m_stcDirectPlace.SetWindowTextW((L"地区: " + (g_data.m_place_direct.empty() ? L"-" : g_data.m_place_direct)).c_str());
    m_stcDirectIsp.SetWindowTextW((L"运营商: " + (g_data.m_isp_direct.empty() ? L"-" : g_data.m_isp_direct)).c_str());
    m_stcDirectThreat.SetWindowTextW(FormatThreatBadge(g_data.m_threat_direct).c_str());

    // 代理卡片
    if (g_data.m_setting_data.use_proxy)
    {
        m_stcProxyIp.SetWindowTextW((L"出口 IP: " + (g_data.m_public_ip_proxy.empty() ? L"-" : g_data.m_public_ip_proxy)).c_str());
        m_stcProxyPlace.SetWindowTextW((L"地区: " + (g_data.m_place_proxy.empty() ? L"-" : g_data.m_place_proxy)).c_str());
        m_stcProxyIsp.SetWindowTextW((L"运营商: " + (g_data.m_isp_proxy.empty() ? L"-" : g_data.m_isp_proxy)).c_str());
        m_stcProxyThreat.SetWindowTextW(FormatThreatBadge(g_data.m_threat_proxy).c_str());
    }
    else
    {
        m_stcProxyIp.SetWindowTextW(L"出口 IP: （未启用代理）");
        m_stcProxyPlace.SetWindowTextW(L"地区: -");
        m_stcProxyIsp.SetWindowTextW(L"运营商: -");
        m_stcProxyThreat.SetWindowTextW(L"网络类型: 未使用代理\n纯净度: -");
    }

    // 完整展示框：将所有可显示信息完整呈现，所见即所拷
    std::wstring fullText;

    // 1. 直连网络信息
    if (!g_data.m_public_ip_direct.empty() || !g_data.m_place_direct.empty())
    {
        fullText += L"直: " + (g_data.m_public_ip_direct.empty() ? L"-" : g_data.m_public_ip_direct);
        if (!g_data.m_place_direct.empty()) fullText += L"  [" + g_data.m_place_direct + L"]";
        if (!g_data.m_isp_direct.empty())   fullText += L"  " + g_data.m_isp_direct;
        fullText += L"\r\n    ";
        {
            std::wstring type;
            const auto& t = g_data.m_threat_direct;
            if (t.is_datacenter) { if (!type.empty()) type += L" + "; type += L"IDC机房"; }
            if (t.is_mobile)     { if (!type.empty()) type += L" + "; type += L"移动网络"; }
            if (t.is_vpn)        { if (!type.empty()) type += L" + "; type += L"VPN"; }
            if (t.is_proxy)      { if (!type.empty()) type += L" + "; type += L"代理"; }
            if (t.is_tor)        { if (!type.empty()) type += L" + "; type += L"Tor"; }
            if (type.empty()) type = L"普通宽带";
            fullText += L"类型: " + type + L" | 状态: " + (t.is_abuser ? L"恶意/黑名单" : L"纯净");
            if (!t.risk_score.empty()) fullText += L" (欺诈分: " + t.risk_score + L")";
        }
        fullText += L"\r\n";
    }

    // 2. 代理出口信息
    if (g_data.m_setting_data.use_proxy)
    {
        if (!g_data.m_public_ip_proxy.empty() || !g_data.m_place_proxy.empty())
        {
            fullText += L"代: " + (g_data.m_public_ip_proxy.empty() ? L"-" : g_data.m_public_ip_proxy);
            if (!g_data.m_place_proxy.empty()) fullText += L"  [" + g_data.m_place_proxy + L"]";
            if (!g_data.m_isp_proxy.empty())   fullText += L"  " + g_data.m_isp_proxy;
            fullText += L"\r\n    ";
            {
                std::wstring type;
                const auto& t = g_data.m_threat_proxy;
                if (t.is_datacenter) { if (!type.empty()) type += L" + "; type += L"IDC机房"; }
                if (t.is_mobile)     { if (!type.empty()) type += L" + "; type += L"移动网络"; }
                if (t.is_vpn)        { if (!type.empty()) type += L" + "; type += L"VPN"; }
                if (t.is_proxy)      { if (!type.empty()) type += L" + "; type += L"代理"; }
                if (t.is_tor)        { if (!type.empty()) type += L" + "; type += L"Tor"; }
                if (type.empty()) type = L"普通宽带";
                fullText += L"类型: " + type + L" | 状态: " + (t.is_abuser ? L"恶意/黑名单" : L"纯净");
                if (!t.risk_score.empty()) fullText += L" (欺诈分: " + t.risk_score + L")";
            }
            fullText += L"\r\n";
        }
    }
    else
    {
        fullText += L"代: （未启用代理）\r\n";
    }

    // 3. 延迟测速
    if (!g_data.m_latency_results.empty())
    {
        fullText += L"延迟:\r\n直: ";
        bool firstD = true;
        for (const auto& r : g_data.m_latency_results)
        {
            if (r.direct_ms >= 0)
            {
                if (!firstD) fullText += L" | ";
                fullText += r.name + L" - " + std::to_wstring((int)(r.direct_ms + 0.5)) + L"ms";
                firstD = false;
            }
        }
        if (firstD) fullText += L"-";
        fullText += L"\r\n代: ";
        if (g_data.m_setting_data.use_proxy)
        {
            bool firstP = true;
            for (const auto& r : g_data.m_latency_results)
            {
                if (r.proxy_ms >= 0)
                {
                    if (!firstP) fullText += L" | ";
                    fullText += r.name + L" - " + std::to_wstring((int)(r.proxy_ms + 0.5)) + L"ms";
                    firstP = false;
                }
            }
            if (firstP) fullText += L"-";
        }
        else
        {
            fullText += L"-";
        }
        fullText += L"\r\n";
    }

    // 4. 更新时间
    std::wstring lastUpdate = g_data.GetLastUpdateStatusString();
    if (!lastUpdate.empty())
        fullText += lastUpdate;

    m_edtStatus.SetWindowTextW(fullText.empty() ? L"（尚无数据，请等待后台刷新完成）" : fullText.c_str());
}

void CTabStatusDlg::CopyToClipboard(const std::wstring& text)
{
    if (text.empty()) return;
    if (OpenClipboard())
    {
        EmptyClipboard();
        size_t size = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, size);
        if (hMem)
        {
            memcpy(GlobalLock(hMem), text.c_str(), size);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
        CloseClipboard();
    }
}

void CTabStatusDlg::OnBnClickedCopyDirect()
{
    std::wstring info;
    info += L"直: " + (g_data.m_public_ip_direct.empty() ? L"-" : g_data.m_public_ip_direct);
    if (!g_data.m_place_direct.empty()) info += L"  [" + g_data.m_place_direct + L"]";
    if (!g_data.m_isp_direct.empty())   info += L"  " + g_data.m_isp_direct;
    CopyToClipboard(info);
}

void CTabStatusDlg::OnBnClickedCopyProxy()
{
    std::wstring info;
    if (g_data.m_setting_data.use_proxy)
    {
        info += L"代: " + (g_data.m_public_ip_proxy.empty() ? L"-" : g_data.m_public_ip_proxy);
        if (!g_data.m_place_proxy.empty()) info += L"  [" + g_data.m_place_proxy + L"]";
        if (!g_data.m_isp_proxy.empty())   info += L"  " + g_data.m_isp_proxy;
    }
    else
    {
        info = L"代: （未启用代理）";
    }
    CopyToClipboard(info);
}

void CTabStatusDlg::OnBnClickedCopyAll()
{
    CString s;
    m_edtStatus.GetWindowTextW(s);
    CopyToClipboard(s.GetString());
}

// ====================================================================
// COptionsDlg —— 主对话框容器
// ====================================================================
IMPLEMENT_DYNAMIC(COptionsDlg, CDialog)

COptionsDlg::COptionsDlg(CWnd* pParent)
    : CDialog(IDD_OPTIONS_DIALOG, pParent)
    , m_pageProxyApi(this)
    , m_pageLatency(this)
    , m_pageStatus(this)
{
    m_pages[0] = &m_pageProxyApi;
    m_pages[1] = &m_pageLatency;
    m_pages[2] = &m_pageStatus;
}

void COptionsDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_TAB_MAIN, m_tabCtrl);
}

BEGIN_MESSAGE_MAP(COptionsDlg, CDialog)
    ON_NOTIFY(TCN_SELCHANGE, IDC_TAB_MAIN, &COptionsDlg::OnTcnSelchangeTabMain)
END_MESSAGE_MAP()

BOOL COptionsDlg::OnInitDialog()
{
    CDialog::OnInitDialog();

    m_tabCtrl.InsertItem(0, L"常规 / 网络");
    m_tabCtrl.InsertItem(1, L"延迟目标");
    m_tabCtrl.InsertItem(2, L"当前状态与安全");

    m_pageProxyApi.Create(IDD_TAB_PROXY_API, this);
    m_pageLatency.Create(IDD_TAB_LATENCY_LIST, this);
    m_pageStatus.Create(IDD_TAB_STATUS_INFO, this);

    PositionPages();
    SwitchToPage(0);

    return TRUE;
}

void COptionsDlg::PositionPages()
{
    CRect tabRect;
    m_tabCtrl.GetWindowRect(&tabRect);
    ScreenToClient(&tabRect);
    m_tabCtrl.AdjustRect(FALSE, &tabRect);
    tabRect.DeflateRect(1, 1);

    for (auto* p : m_pages)
    {
        if (p && p->GetSafeHwnd())
            p->MoveWindow(tabRect);
    }
}

void COptionsDlg::SwitchToPage(int idx)
{
    for (int i = 0; i < 3; ++i)
    {
        if (m_pages[i] && m_pages[i]->GetSafeHwnd())
            m_pages[i]->ShowWindow(i == idx ? SW_SHOW : SW_HIDE);
    }
    m_curPage = idx;

    if (idx == 2)
    {
        m_pageStatus.UpdateStatusView();
    }
}

void COptionsDlg::OnTcnSelchangeTabMain(NMHDR* /*pNMHDR*/, LRESULT* pResult)
{
    SwitchToPage(m_tabCtrl.GetCurSel());
    *pResult = 0;
}

void COptionsDlg::OnOK()
{
    SettingData& d = g_data.m_setting_data;
    m_pageProxyApi.GetData(d);
    m_pageLatency.GetData(d);

    g_data.SaveConfig();

    CDialog::OnOK();
}
