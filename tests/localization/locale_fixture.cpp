#include "../../pch.h"
#include "../../OptionsDlg.h"
#include "../../UiLanguage.h"

extern "C" __declspec(dllexport) BOOL TestLocalizedDialogs(BOOL chinese)
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
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
                !g_data.IsUpdating();
            dialog.DestroyWindow();
        }
    }
    parent.Detach();
    DestroyWindow(handle);
    return passed;
}
