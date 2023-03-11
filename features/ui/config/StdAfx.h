/***************************************************************************
 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.
***************************************************************************/

// stdafx.h : include file for standard system include files,
//      or project specific include files that are used frequently,
//      but are changed infrequently

#pragma once

#define STRICT


// #define _ATL_APARTMENT_THREADED

#include "emulator.h"
#include "config.h"
#include "resource.h"
#include "iphlpapi.h"
#include "board.h"
#include "wincontroller.h"

#pragma warning (disable:4995) // name was marked as #pragma deprecated
#include <algorithm>
// #include <functional>
#include <exception>
// #include <limits>
#include <vector>
#include <map>

#include <initguid.h>   // make DEFINE_GUID create actual GUID variables, for oleacc
#include <atlbase.h>
#include <atlcom.h>
#include <atlctrls.h>
#include <atlwin.h>
#include <atlstr.h>
#include <atlimage.h>
#include <oleacc.h>
#include <shlguid.h>
#pragma warning(default:4995)

#include "xmlskin.h"

#include "confighelp.h"
#include "dcpexception.h"
#include "browse.h"
#include "checkeddialog.h"
#include "dsstrings.h"
#include "propertymap.h"
#include "tabpage.h"
#include "configdlg.h"
