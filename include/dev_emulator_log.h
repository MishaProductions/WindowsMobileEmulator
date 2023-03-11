/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef DEV_EMULATOR_H__
#define DEV_EMULATOR_H__

// Include definition for logging levels and the logging macro
#include "vsd_logging.h"

#ifdef ENABLED_WMI_LOGGING
// Define the control guids and flags
#define WPP_CONTROL_GUIDS \
    WPP_DEFINE_CONTROL_GUID(DeviceEmulatorCtlGuid,(F836FFA2,9211,487e,9563,79426ED635A3), \
        WPP_DEFINE_BIT(GENERAL)                \
        WPP_DEFINE_BIT(NETWORK)                \
        WPP_DEFINE_BIT(DMA)                    \
        WPP_DEFINE_BIT(FOLDERSHARING)          \
        WPP_DEFINE_BIT(AUDIO)                  \
        WPP_DEFINE_BIT(PERIPHERAL_CALLS)       \
        WPP_DEFINE_BIT(CACHE)                  \
    )               

#define WPP_DLL // Uncomment for a DLL component (requires _WIN32_WINNT >= 0x0500)
#endif // ENABLED_WMI_LOGGING

#endif // DEV_EMULATOR_H__


