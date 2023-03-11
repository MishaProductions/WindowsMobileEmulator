/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef NETWORK_H
#define NETWORK_H

// Network.h : Declaration of CNetwork
// Simple owner class for the adapter array - frees the memory on destruction
class AdapterInfoArray
{
public:
    // Allocate enough IP_ADAPTER_INFO objects to ensure that the size is at least n bytes
    AdapterInfoArray(int n) : m_pAdapters(new IP_ADAPTER_INFO[(n+sizeof(IP_ADAPTER_INFO)-1)/sizeof(IP_ADAPTER_INFO)]) { }
    ~AdapterInfoArray() { delete[] m_pAdapters; }
    IP_ADAPTER_INFO* MatchMAC(unsigned __int8 * MAC)
    {
        IP_ADAPTER_INFO* pAdapter = NULL;
        for (pAdapter= m_pAdapters;pAdapter!=0;pAdapter=pAdapter->Next) {
            if (pAdapter->AddressLength == 6 )
            {
                unsigned __int8 * addr = pAdapter->Address;
                if ((*reinterpret_cast<const unsigned __int32 *>(&addr[0]) == *reinterpret_cast<const unsigned __int32 *>(&MAC[0])) &&
                    (*reinterpret_cast<const unsigned __int16 *>(&addr[4]) == *reinterpret_cast<const unsigned __int16 *>(&MAC[4])))
                break;
            }
        }
        return pAdapter;
    }
    IP_ADAPTER_INFO* m_pAdapters;
};

class CNetwork :
    public CCheckedDialog<CNetwork>, public ITabPage
{
public:
    CNetwork(PropertyMap& properties) : ITabPage(properties) { adapters = NULL; }
    ~CNetwork() { if (adapters != NULL) delete adapters; }
    int GetNameResourceID() { return IDS_EMULNETWORKTAB; }
    CString GetHelpKeyword() { return HELPKEYWORD_VSD_CONFIGEMUNETWORK; }

    enum { IDD = IDD_EMCONFIGNETWORKPAGE };

BEGIN_MSG_MAP(CNetwork)
    MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
    COMMAND_HANDLER(IDC_NE2000, BN_CLICKED, OnClickedNE2000)
    COMMAND_HANDLER(IDC_CS8900, BN_CLICKED, OnClickedCS8900)
//    CHAIN_MSG_MAP_ALT(CDialogImpl<CNetwork>,0)
END_MSG_MAP()

    LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnClickedNE2000(WORD, WORD, HWND, BOOL&) { SetNE2000Enable(); return 0; }
    LRESULT OnClickedCS8900(WORD, WORD, HWND, BOOL&) { SetCS8900Enable(); return 0; }

    void CommitChanges();
    bool BValidating();
private:
    void SetNE2000Enable()  
      { m_comboNE2000.EnableWindow(m_checkNE2000.GetCheck() && m_properties[g_strDSPropertyNE2000Adapter].IsEnabled()); }
    void SetCS8900Enable() 
      { m_comboCS8900.EnableWindow(m_checkCS8900.GetCheck() && m_properties[g_strDSPropertyCS8900Adapter].IsEnabled()); }

    CWinButton m_checkNE2000;
    CWinComboBox m_comboNE2000;
    CWinButton m_checkCS8900;
    CWinComboBox m_comboCS8900;
    CWinButton m_checkHostOnly;
    AdapterInfoArray * adapters;
};

#endif // NETWORK_H
