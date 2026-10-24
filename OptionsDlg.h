#pragma once
#include <afxcmn.h>
#include "resource.h"
#include "DataManager.h"
#include <vector>

// ────────────────────────────────────────────────────────────────────────
// Tab 0: 常规 / 网络 & 代理 设置
// ────────────────────────────────────────────────────────────────────────
class CTabProxyApiDlg : public CDialog
{
    DECLARE_DYNAMIC(CTabProxyApiDlg)
public:
    explicit CTabProxyApiDlg(CWnd* pParent = nullptr);
    enum { IDD = IDD_TAB_PROXY_API };

    void GetData(SettingData& out) const;

protected:
    virtual BOOL OnInitDialog() override;
    virtual void DoDataExchange(CDataExchange* pDX) override;
    virtual void OnOK() override {}
    virtual void OnCancel() override {}

    afx_msg void OnBnClickedUseProxy();
    afx_msg void OnBnClickedRefreshNow();
    afx_msg void OnBnClickedTestProxy();
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    DECLARE_MESSAGE_MAP()

private:
    void UpdateProxyEditState();

    CComboBox m_cmbDisplayFormat;
    CButton   m_chkUseProxy;
    CEdit     m_edtProxy;
    CEdit     m_edtDomesticApi;
    CEdit     m_edtForeignApi;
    CEdit     m_edtInterval;
    CButton   m_btnRefresh;
    CStatic   m_stcStatus;
    CButton   m_btnTestProxy;
    CStatic   m_stcTestProxyStatus;
};

// ────────────────────────────────────────────────────────────────────────
// Tab 1: 延迟目标设置（动态 ListView 报表，带连通性与增删排）
// ────────────────────────────────────────────────────────────────────────
class CTabLatencyDlg : public CDialog
{
    DECLARE_DYNAMIC(CTabLatencyDlg)
public:
    explicit CTabLatencyDlg(CWnd* pParent = nullptr);
    enum { IDD = IDD_TAB_LATENCY_LIST };

    void GetData(SettingData& out) const;

protected:
    virtual BOOL OnInitDialog() override;
    virtual void DoDataExchange(CDataExchange* pDX) override;
    virtual void OnOK() override {}
    virtual void OnCancel() override {}

    afx_msg void OnBnClickedAdd();
    afx_msg void OnBnClickedDel();
    afx_msg void OnBnClickedUp();
    afx_msg void OnBnClickedDown();
    afx_msg void OnBnClickedTest();
    afx_msg void OnNMClickList(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnNMDblclkList(NMHDR* pNMHDR, LRESULT* pResult);
    DECLARE_MESSAGE_MAP()

private:
    void RefreshList();
    void LoadSelectedToEdit();

    CListCtrl m_listCtrl;
    CEdit     m_edtName;
    CEdit     m_edtUrl;
    std::vector<LatencyTarget> m_targets;
};

// ────────────────────────────────────────────────────────────────────────
// Tab 2: 当前状态与安全检测报告（卡片式对照 + 一键复制）
// ────────────────────────────────────────────────────────────────────────
class CTabStatusDlg : public CDialog
{
    DECLARE_DYNAMIC(CTabStatusDlg)
public:
    explicit CTabStatusDlg(CWnd* pParent = nullptr);
    enum { IDD = IDD_TAB_STATUS_INFO };

    void UpdateStatusView();

protected:
    virtual BOOL OnInitDialog() override;
    virtual void DoDataExchange(CDataExchange* pDX) override;
    virtual void OnOK() override {}
    virtual void OnCancel() override {}

    afx_msg void OnBnClickedCopyDirect();
    afx_msg void OnBnClickedCopyProxy();
    afx_msg void OnBnClickedCopyAll();
    DECLARE_MESSAGE_MAP()

private:
    void CopyToClipboard(const std::wstring& text);

    CStatic m_stcDirectIp;
    CStatic m_stcDirectPlace;
    CStatic m_stcDirectIsp;
    CStatic m_stcDirectThreat;

    CStatic m_stcProxyIp;
    CStatic m_stcProxyPlace;
    CStatic m_stcProxyIsp;
    CStatic m_stcProxyThreat;

    CEdit   m_edtStatus;
};

// ────────────────────────────────────────────────────────────────────────
// 主对话框：Tab 容器
// ────────────────────────────────────────────────────────────────────────
class COptionsDlg : public CDialog
{
    DECLARE_DYNAMIC(COptionsDlg)
public:
    explicit COptionsDlg(CWnd* pParent = nullptr);
    enum { IDD = IDD_OPTIONS_DIALOG };

protected:
    virtual BOOL OnInitDialog() override;
    virtual void OnOK() override;
    virtual void DoDataExchange(CDataExchange* pDX) override;

    afx_msg void OnTcnSelchangeTabMain(NMHDR* pNMHDR, LRESULT* pResult);
    DECLARE_MESSAGE_MAP()

private:
    void SwitchToPage(int idx);
    void PositionPages();

    CTabCtrl        m_tabCtrl;
    CTabProxyApiDlg m_pageProxyApi;
    CTabLatencyDlg  m_pageLatency;
    CTabStatusDlg   m_pageStatus;
    CDialog*        m_pages[3];
    int             m_curPage{ 0 };
};
