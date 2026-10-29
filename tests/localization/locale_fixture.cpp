#include "../../pch.h"
#include "../../OptionsDlg.h"
#include "../../UiLanguage.h"
#include <vector>

struct LocalizationTestAccess
{
    static bool HasHan(const std::wstring& value)
    {
        for (wchar_t character : value) if (character >= 0x4e00 && character <= 0x9fff) return true;
        return false;
    }
    static BOOL CALLBACK CheckControl(HWND window, LPARAM parameter)
    {
        // These controls contain provider data, which must not be translated.
        const int id = GetDlgCtrlID(window);
        if (id == IDC_STAT_DIRECT_PLACE || id == IDC_STAT_PROXY_PLACE ||
            id == IDC_STAT_DIRECT_ISP || id == IDC_STAT_PROXY_ISP || id == IDC_STATUS_INFO_EDIT)
            return TRUE;
        wchar_t text[1024]{};
        GetWindowTextW(window, text, 1024);
        if (HasHan(text)) *reinterpret_cast<bool*>(parameter) = false;
        return TRUE;
    }
    static bool CheckUi(COptionsDlg& dialog, bool chinese)
    {
        bool passed = true;
        if (!chinese)
        {
            EnumChildWindows(dialog.m_hWnd, &CheckControl, reinterpret_cast<LPARAM>(&passed));
            for (const auto& tab : dialog.m_tabBar.m_tabs) passed = passed && !HasHan(tab);
            for (const auto* page : { static_cast<CModernSubDlg*>(&dialog.m_pageProxyApi),
                static_cast<CModernSubDlg*>(&dialog.m_pageLatency), static_cast<CModernSubDlg*>(&dialog.m_pageStatus) })
                for (const auto& card : page->m_cards) passed = passed && !HasHan(card.title);
        }
        auto* list = static_cast<CListCtrl*>(dialog.m_pageLatency.GetDlgItem(IDC_LATENCY_LIST));
        for (int index = 0; index < 4; ++index)
        {
            wchar_t text[128]{};
            HDITEMW column{};
            column.mask = HDI_TEXT; column.pszText = text; column.cchTextMax = 128;
            passed = passed && list->GetHeaderCtrl()->GetItem(index, &column);
            if (!chinese) passed = passed && !HasHan(text);
        }
        SettingData roundtrip = g_data.GetSettings();
        dialog.m_pageProxyApi.GetData(roundtrip);
        dialog.m_pageLatency.GetData(roundtrip);
        const auto original = g_data.GetSettings();
        passed = passed && roundtrip.latency_targets.size() == original.latency_targets.size();
        for (size_t index = 0; index < original.latency_targets.size(); ++index)
            passed = passed && roundtrip.latency_targets[index].name == original.latency_targets[index].name &&
                roundtrip.latency_targets[index].url == original.latency_targets[index].url;
        return passed;
    }
};

extern "C" __declspec(dllexport) BOOL TestLocalizedDialogs(BOOL chinese)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    const auto settings = g_data.GetSettings();
    auto custom = settings;
    custom.latency_targets.push_back({ L"用户节点", L"https://example.test/?name=中文" });
    g_data.SetSettings(std::move(custom));
    HWND handle = CreateWindowExW(0, L"STATIC", L"test", WS_POPUP,
        0, 0, 1000, 700, nullptr, nullptr, nullptr, nullptr);
    if (!handle) return FALSE;
    CWnd parent;
    parent.Attach(handle);
    bool passed = false;
    {
        COptionsDlg dialog(&parent);
        if (dialog.CreateIndirect(UiLanguage::Dialog(IDD_OPTIONS_DIALOG), &parent))
        {
            CString caption, ok, cancel;
            dialog.GetWindowTextW(caption);
            dialog.GetDlgItem(IDOK)->GetWindowTextW(ok);
            dialog.GetDlgItem(IDCANCEL)->GetWindowTextW(cancel);
            passed = caption == (chinese ? L"IP 地区与网络监控设置" : L"IP Region & Latency Monitor Settings") &&
                ok == (chinese ? L"确定" : L"OK") && cancel == (chinese ? L"取消" : L"Cancel") &&
                !g_data.IsUpdating() && LocalizationTestAccess::CheckUi(dialog, chinese != FALSE);
            dialog.DestroyWindow();
        }
    }
    parent.Detach();
    DestroyWindow(handle);
    g_data.SetSettings(settings);
    return passed;
}
