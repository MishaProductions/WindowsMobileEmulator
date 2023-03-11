/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef SAXERRORHANDLERIMPL_H
#define SAXERRORHANDLERIMPL_H
#pragma once

#include <msxml2.h>

class SAXErrorHandlerImpl : public ISAXErrorHandler  
{
public:
	SAXErrorHandlerImpl();
	virtual ~SAXErrorHandlerImpl();

		// This must be correctly implemented, if your handler must be a COM Object (in this example it does not)
		long __stdcall QueryInterface(const struct _GUID &,void ** );
		unsigned long __stdcall AddRef(void);
		unsigned long __stdcall Release(void);

        virtual HRESULT STDMETHODCALLTYPE error( 
            /* [in] */ ISAXLocator __RPC_FAR *pLocator,
            /* [in] */ const wchar_t * pwchErrorMessage,
			/* [in] */ HRESULT errCode);
        
        virtual HRESULT STDMETHODCALLTYPE fatalError( 
            /* [in] */ ISAXLocator __RPC_FAR *pLocator,
            /* [in] */ const wchar_t * pwchErrorMessage,
			/* [in] */ HRESULT errCode);
        
        virtual HRESULT STDMETHODCALLTYPE ignorableWarning( 
            /* [in] */ ISAXLocator __RPC_FAR *pLocator,
            /* [in] */ const wchar_t * pwchErrorMessage,
			/* [in] */ HRESULT errCode);

};

#endif	// SAXERRORHANDLERIMPL_H

