/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef BROWSE_H
#define BROWSE_H

void GetFileFromBrowse(const CString& strStartPath,int idsFilter,CWindow& winPath,HWND hwndOwner);
void GetDirectoryFromBrowse(const CString& strStartPath, CWindow& winPath,HWND hwndOwner);

// Used to set the names on the browse buttons for accessebility
void setAANameOnControl(DWORD NameID, HWND hwnd);

#endif // BROWSE_H
