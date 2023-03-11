/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef DCPEXCEPTION_H
#define DCPEXCEPTION_H

// DCPException.h : declaration of the CDCPException class and exception helper macros

class CDCPException
{
public:
    // Constructors
    CDCPException();
    CDCPException(HRESULT hr);
    CDCPException(const std::exception& serr);
    CDCPException(int i) : m_hr(E_FAIL) { InitMessage(i); }
    CDCPException(const CString& strMsg) : m_strMsg(strMsg), m_hr(E_FAIL) { }

    // Output functions
    void Display() const;
    HRESULT GetHR() const { return m_hr; }
private:
    // Helper function
    void InitMessage(int i);

    CString m_strMsg;
    HRESULT m_hr;
};

#define VERIFYTHROW(fTest, szMsg, exception)                        \
  do                                                                \
    {                                                               \
    if (!(fTest))                                                   \
      {                                                             \
      ASSERT(false);                                                \
      throw exception;                                              \
      }                                                             \
    }                                                               \
  while (false)                                                     \

#define IfErrorThrow(exp)        do { HRESULT hrMACRO=(exp); VERIFYTHROW(SUCCEEDED(hrMACRO),"",CDCPException(hrMACRO)); } while (false)
#define IfFalseThrow(exp,hr)     VERIFYTHROW((exp),"",CDCPException((hr)))
#define IfZeroThrow(exp,hr)      IfFalseThrow((exp)!=0,(hr))
#define IfNullThrow(exp,hr)      IfZeroThrow((exp),(hr))
#define IfNegativeThrow(exp,hr)  IfFalseThrow((exp)>=0,(hr))

// These macros transparently catch other forms of exception (std::exception)
// and convert them to CDCPException.
#define DCPTRY try { try
#define DCPCATCH(x)                     \
        catch (std::exception& serr) {  \
            throw CDCPException(serr);  \
        }                               \
    }                                   \
    catch (x)

#endif // DCPEXCEPTION_H
