#include "pch.h"
#include "CustomDrawItem.h"
#include "DataManager.h"

const wchar_t* CCustomDrawItem::GetItemName() const
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    return g_data.StringRes(m_is_proxy ? IDS_PROXY_ITEM_NAME : IDS_DIRECT_ITEM_NAME);
}

const wchar_t* CCustomDrawItem::GetItemId() const
{
    return m_is_proxy ? L"4Tc21hGS_proxy" : L"4Tc21hGS_direct";
}

const wchar_t* CCustomDrawItem::GetItemLableText() const
{
    return g_data.StringRes(m_is_proxy ? IDS_PROXY_LABEL : IDS_DIRECT_LABEL);
}

const wchar_t* CCustomDrawItem::GetItemValueText() const
{
    AFX_MANAGE_STATE(AfxGetStaticModuleState());
    return g_data.GetFormattedItemText(m_is_proxy);
}

const wchar_t* CCustomDrawItem::GetItemValueSampleText() const
{
    return m_is_proxy ? L"US Los Angeles (1.1.1.1)" : L"CN 上海 (220.181.38.148)";
}
