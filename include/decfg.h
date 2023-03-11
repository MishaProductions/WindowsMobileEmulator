/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef DECFG_H
#define DECFG_H

void __fastcall ShowConfigDialog(HWND hwndParent);
HRESULT __fastcall ShowConfigDialog(HWND hwndParent,BSTR bstrConfig,BSTR* pbstrConfig);

#endif // DECFG_H
