/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#include "SAXErrorHandlerImpl.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

SAXErrorHandlerImpl::SAXErrorHandlerImpl()
{

}

SAXErrorHandlerImpl::~SAXErrorHandlerImpl()
{

}

HRESULT STDMETHODCALLTYPE SAXErrorHandlerImpl::error( 
            /* [in] */ ISAXLocator __RPC_FAR * /*pLocator*/,
            /* [in] */ const wchar_t * /*pwchErrorMessage*/,
			/* [in] */ HRESULT /*errno_t*/)
{
	return S_OK;
}
        
HRESULT STDMETHODCALLTYPE SAXErrorHandlerImpl::fatalError( 
            /* [in] */ ISAXLocator __RPC_FAR * /*pLocator*/,
            /* [in] */ const wchar_t * /*pwchErrorMessage*/,
			/* [in] */ HRESULT /*errno_t*/)
{
	return S_OK;
}
        
HRESULT STDMETHODCALLTYPE SAXErrorHandlerImpl::ignorableWarning( 
            /* [in] */ ISAXLocator __RPC_FAR * /*pLocator*/,
            /* [in] */ const wchar_t * /*pwchErrorMessage*/,
			/* [in] */ HRESULT /*errno_t*/)
{
	return S_OK;
}

long __stdcall SAXErrorHandlerImpl::QueryInterface(const struct _GUID &,void ** )
{
	return 0;
}

unsigned long __stdcall SAXErrorHandlerImpl::AddRef()
{
	return 0;
}

unsigned long __stdcall SAXErrorHandlerImpl::Release()
{
	return 0;
}

