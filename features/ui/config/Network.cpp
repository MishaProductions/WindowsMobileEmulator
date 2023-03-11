/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

// Network.cpp : Implementation of CNetwork

#include "stdafx.h"

void ConvertStringToMAC( __in_z LPTSTR value, unsigned __int8 * buffer)
{
    ASSERT( wcslen(value) == 12 );
    if (wcslen(value) != 12)
        return;
    for (int b_i = 0; b_i < 6; b_i++ )
    {
        wchar_t temp[3];
        temp[0] = value[b_i*2];
        temp[1] = value[b_i*2+1];
        temp[2] = 0;

        buffer[b_i] = (unsigned __int8)wcstoul(temp, NULL, 16);
    }
}

LRESULT CNetwork::OnInitDialog(UINT, WPARAM, LPARAM, BOOL&)
{
    CheckedAttach(IDC_NE2000,m_checkNE2000);
    CheckedAttach(IDC_NE2000COMBO,m_comboNE2000);
    CheckedAttach(IDC_CS8900,m_checkCS8900);
    CheckedAttach(IDC_CS8900COMBO,m_comboCS8900);
    CheckedAttach(IDC_HOSTONLYNET,m_checkHostOnly);

    m_checkNE2000.SetCheck(m_properties[g_strDSPropertyEthernetEnabled].CheckedIfTrue());
    m_checkNE2000.ShowWindow(m_properties[g_strDSPropertyEthernetEnabled].Visible());
    m_checkCS8900.SetCheck(m_properties[g_strDSPropertyCS8900EthernetEnabled].CheckedIfTrue());
    m_checkCS8900.ShowWindow(m_properties[g_strDSPropertyCS8900EthernetEnabled].Visible());
    m_checkCS8900.EnableWindow(m_properties[g_strDSPropertyCS8900EthernetEnabled].IsEnabled());
    m_checkHostOnly.SetCheck(m_properties[g_strDSPropertyHostOnlyEthernetEnabled].CheckedIfTrue());
    m_checkHostOnly.ShowWindow(m_properties[g_strDSPropertyHostOnlyEthernetEnabled].Visible());

    CString default_str;
    int i;
    IfZeroThrow(default_str.LoadString(IDS_DEFAULT),E_FAIL);
    i = m_comboNE2000.AddString(default_str);
    m_comboNE2000.SetItemDataPtr(i,NULL);
    i = m_comboCS8900.AddString(default_str);
    m_comboCS8900.SetItemDataPtr(i,NULL);

    ULONG outBufLen=0;
    DWORD dwResult = GetAdaptersInfo(NULL,&outBufLen);
    IfFalseThrow((dwResult == ERROR_BUFFER_OVERFLOW || dwResult == ERROR_NO_DATA ||
                  dwResult == ERROR_SUCCESS),E_FAIL);
    if ( dwResult == ERROR_BUFFER_OVERFLOW )
    {
        adapters = new AdapterInfoArray(outBufLen);
        IfFalseThrow(GetAdaptersInfo(adapters->m_pAdapters,&outBufLen)==ERROR_SUCCESS,E_FAIL);

        for (IP_ADAPTER_INFO* pAdapter=reinterpret_cast<IP_ADAPTER_INFO*>(adapters->m_pAdapters);pAdapter!=0;pAdapter=pAdapter->Next) {
            i = m_comboNE2000.AddString(CA2T(pAdapter->Description));
            m_comboNE2000.SetItemDataPtr(i,pAdapter);
            i = m_comboCS8900.AddString(CA2T(pAdapter->Description));
            m_comboCS8900.SetItemDataPtr(i,pAdapter);
        }
    }

    unsigned __int64 NE2000MacBuffer = 0;
    unsigned __int8 * NE2000Mac = (unsigned __int8 *)&NE2000MacBuffer;
    unsigned __int64 CS8900MacBuffer = 0;
    unsigned __int8 * CS8900Mac = (unsigned __int8 *)&CS8900MacBuffer;

    if ( m_properties[g_strDSPropertyNE2000Adapter].Value().GetLength() == 12 )
        ConvertStringToMAC(m_properties[g_strDSPropertyNE2000Adapter].Value().GetBuffer(), NE2000Mac);
    if ( m_properties[g_strDSPropertyCS8900Adapter].Value().GetLength() == 12 )
        ConvertStringToMAC(m_properties[g_strDSPropertyCS8900Adapter].Value().GetBuffer(), CS8900Mac);

    IP_ADAPTER_INFO* cAdapter = NULL;
    // Check if we are using the default option or the current MAC address is unavailable
    if ( NE2000MacBuffer == 0 || adapters == NULL || ( cAdapter = adapters->MatchMAC(NE2000Mac) ) == NULL )
        IfFalseThrow(m_comboNE2000.SelectString(-1,default_str)!=LB_ERR,E_FAIL);
    else
    {
        int curr_item;
        for (curr_item = 0; curr_item < m_comboNE2000.GetCount(); curr_item++)
        {
            if (m_comboNE2000.GetItemDataPtr(curr_item) == (void *)cAdapter)
            {
                IfFalseThrow(m_comboNE2000.SetCurSel(curr_item) != LB_ERR, E_FAIL);
                break;
            }
        }
        ASSERT( curr_item != m_comboNE2000.GetCount() );
        // If for some reason we failed to find a match (the card has been uninstalled) pick the default
        if (curr_item == m_comboNE2000.GetCount())
            IfFalseThrow(m_comboNE2000.SelectString(-1,default_str)!=LB_ERR,E_FAIL);
    }
    // Check if we are using the default option or a card which is not currently inserted
    if ( CS8900MacBuffer == 0 || adapters == NULL || ( cAdapter = adapters->MatchMAC(CS8900Mac) ) == NULL )
       IfFalseThrow(m_comboCS8900.SelectString(-1,default_str)!=LB_ERR,E_FAIL);
    else
    {
        int curr_item;
        for (curr_item = 0; curr_item < m_comboNE2000.GetCount(); curr_item++)
        {
            if (m_comboCS8900.GetItemDataPtr(curr_item) == (void *)cAdapter)
            {
                IfFalseThrow(m_comboCS8900.SetCurSel(curr_item) != LB_ERR, E_FAIL);
                break;
            }
        }
        ASSERT( curr_item != m_comboCS8900.GetCount() );
        // If for some reason we failed to find a match (the card has been uninstalled) pick the default
        if ( curr_item == m_comboCS8900.GetCount() )
           IfFalseThrow(m_comboCS8900.SelectString(-1,default_str)!=LB_ERR,E_FAIL);
    }
    SetNE2000Enable();
    SetCS8900Enable();

    return 1;  // Let the system set the focus
}

bool CNetwork::BValidating()
{
    return true;
}

void CNetwork::CommitChanges()
{
    m_properties[g_strDSPropertyEthernetEnabled].SetValue(m_checkNE2000.GetCheck());
    m_properties[g_strDSPropertyCS8900EthernetEnabled].SetValue(m_checkCS8900.GetCheck());
    m_properties[g_strDSPropertyHostOnlyEthernetEnabled].SetValue(m_checkHostOnly.GetCheck());

    unsigned __int64 TempMacBuffer = 0;
    wchar_t Buffer[14];

    int i = m_comboNE2000.GetCurSel();
    VERIFYTHROW(i!=LB_ERR,"No item selected in NE2000 combo box",CDCPException());
    IP_ADAPTER_INFO* cAdapter = (IP_ADAPTER_INFO*)(m_comboNE2000.GetItemDataPtr(i));
    if ( cAdapter == NULL || cAdapter->AddressLength != 6 )
    {
        m_properties[g_strDSPropertyNE2000Adapter].SetValue(CString(MACToString(Buffer, (unsigned __int8 *)&TempMacBuffer)));
    }
    else
        m_properties[g_strDSPropertyNE2000Adapter].SetValue(CString(MACToString(Buffer, cAdapter->Address)));

    i = m_comboCS8900.GetCurSel();
    VERIFYTHROW(i!=LB_ERR,"No item selected in CS8900 combo box",CDCPException());
    cAdapter = (IP_ADAPTER_INFO*)(m_comboCS8900.GetItemDataPtr(i));
    if ( cAdapter == NULL || cAdapter->AddressLength != 6 )
        m_properties[g_strDSPropertyCS8900Adapter].SetValue(CString(MACToString(Buffer, (unsigned __int8 *)&TempMacBuffer)));
    else
        m_properties[g_strDSPropertyCS8900Adapter].SetValue(CString(MACToString(Buffer, cAdapter->Address)));

}
