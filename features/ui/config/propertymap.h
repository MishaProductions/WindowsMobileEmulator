/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef PROPERTYMAP_H
#define PROPERTYMAP_H

class CMarshalledProperty
{
public:
    CMarshalledProperty() : m_visible(true), m_strValue(L"") { }
    // This constructor is used during the COM call so all values can be changed
    CMarshalledProperty(CString strQuotedValue,CString strVisible )
      : m_strValue(Unquote(strQuotedValue)),
        m_visible(strVisible[0]==L'1' || strVisible[0]==L'2'),
        m_enabled(strVisible[0]==L'1')
    { }
    // This constructor is used during the run time so the string is not quoted
    CMarshalledProperty(CString strQuotedValue, CString strVisible, bool Enabled )
      : m_strValue(strQuotedValue),
        m_visible(strVisible[0]==L'1' || strVisible[0]==L'2'),
        m_enabled(Enabled)
    { }
    int Visible() const { return m_visible ? SW_SHOW : SW_HIDE; }
    CString Value() const { return m_strValue; }
    bool IsEnabled() const { return m_enabled; }
    bool IsTrue() const
    {
        return (
          m_strValue.CompareNoCase(g_strDSValueTrue)==0 ||
          m_strValue.CompareNoCase(g_strDSValueYes)==0 ||
          m_strValue.CompareNoCase(g_strDSValueOn)==0 ||
          m_strValue.CompareNoCase(g_strDSValue1)==0);
    }
    int CheckedIfTrue() const
    {
        return IsTrue() ? BST_CHECKED : BST_UNCHECKED;
    }
    void SetValue(CString str) { m_strValue=str; }
    void SetValue(bool b) { m_strValue=(b ? g_strDSValueYes : g_strDSValueNo); }
    // void SetValueTF(bool b) { m_strValue=b ? g_strDSValueTrue : g_strDSValueFalse; }

    static CString Unquote(CString strQuoted)
    {
        strQuoted.Replace(L"\\,",L"|");
        strQuoted.Replace(L"\\.",L"\\");
        return strQuoted;
    }
private:
    CString m_strValue;
    bool m_visible;
    bool m_enabled;
};

// This class accumulates quoted visibility/property/value triplets
class CAccumulateProperties
{
public:
    CAccumulateProperties() { }
    void operator()(std::pair<CString,CMarshalledProperty> property)
    {
        m_str+=
            Quote(CString(property.first))+
            CString(L"|")+
            Quote(CString(property.second.Value()))+
            CString(property.second.Visible()==SW_SHOW ? L"|1|" : L"|0|");
    }
    operator CComBSTR() { return CComBSTR(m_str); }
private:
    static CString Quote(CString strUnquoted)
    {
        strUnquoted.Replace(L"\\",L"\\.");
        strUnquoted.Replace(L"|",L"\\,");
        return strUnquoted;
    }

    CString m_str;
};

typedef std::map<CString,CMarshalledProperty> PropertyMap;

PropertyMap ParsePropertyString(CString str);
PropertyMap ConfigurationToPropertyString(EmulatorConfig * config);
wchar_t * MACToString( __out_ecount(13) wchar_t * buffer, unsigned __int8 * mac_address );
bool PropertyMapToEmulatorConfig(PropertyMap properites, EmulatorConfig * config);

#endif // PROPERTYMAP_H
