/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

/*
 This file contains a wrapper for including automatically generated TMH files necessary for WMI logging. This
 allows us to deal with any compiler issues centrally.
*/ 

#ifdef ENABLED_WMI_LOGGING
// Check if the filename is initialize
#ifndef TMH_FILENAME
#error You must define TMH_FILENAME before including this wrapper file
#endif
// Include the tmh file
#pragma warning (push)
#pragma warning (disable:4061) //incomplete switch statement
#pragma warning (disable:4068) //prefast
#include TMH_FILENAME
#undef TMH_FILENAME
#pragma warning(pop)
#endif // ENABLED_WMI_LOGGING
