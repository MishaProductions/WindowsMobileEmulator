/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef _VFP_H_INCLUDED
#define _VFP_H_INCLUDED

// VFP coprocessor interface - coprocessors 10 and 11
extern unsigned __int8* __fastcall PlaceVFPCoprocDataTransfer10(unsigned __int8* CodeLocation, Decoded* d);
extern unsigned __int8* __fastcall PlaceVFPCoprocRegisterTransfer10(unsigned __int8* CodeLocation, Decoded* d);
extern unsigned __int8* __fastcall PlaceVFPCoprocDataOperation10(unsigned __int8* CodeLocation, Decoded* d);

extern unsigned __int8* __fastcall PlaceVFPCoprocDataTransfer11(unsigned __int8* CodeLocation, Decoded* d);
extern unsigned __int8* __fastcall PlaceVFPCoprocRegisterTransfer11(unsigned __int8* CodeLocation, Decoded* d);
extern unsigned __int8* __fastcall PlaceVFPCoprocDataOperation11(unsigned __int8* CodeLocation, Decoded* d);
#endif //_VFP_H_INCLUDED
