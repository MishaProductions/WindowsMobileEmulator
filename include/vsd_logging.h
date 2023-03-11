/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

/*
 This file defines logging level for VSD. All the VSD component owners should include it in their macro
 logging macro definition file. The control guids for all components are in vsd_logging.ctl.
*/ 

#ifndef VSD_LOGGING_H__
#define VSD_LOGGING_H__

/* Guidelines for using Levels: 

There are six different logging levels. They are intended to allow usefull logging of all VSD components, 
detailed logging of individual components, stress logging for errors/fatal conditions and 
informational logging across a few component to assist with debugging investigations. 

CRITICAL - this not a true logging level, instead this a modifier that can be used to allow a multi level 
logging statement. The statement would normally have a fixed level but if certain condition were 
true its level would change to critical. In some cases this is convinient way to make the code look cleaner: 

hr = Foo(a, b)
CUSTOM_LOG( INFORMATION, FAILED(hr), "Called foo and got back - %x", hr );

If a message is elevated to critical, it must indicate a bug and a condition that is never expected.
 
FATAL - this is the gravest level, there should be few message at this level. A FATAL message must indicate a bug. 

ERROR - this level indicates a condition that could be caused by either a bug or bad environment. 
This is the level at which stress runs are logged. 

WARNING - this level indicates a condition that expected, however it is somewhat unusual - such as null arguments, 
missing resources, etc. 

INFORMATION - this level is used to record general bird eye messages about component state. This is the highest 
level that must be useful for logging all of the VSD components at the same time. These messages answer the
question - what is going on in the system ?

VERBOSE - this level is used to record detailed messages about component state. For example the messages about 
successful completion of Sent/Receive calls of DMA packets would be at this level. There should be many of 
this and but level must to be useful for logging across a few components at a time. These messages answer the
question - what is going on in the component ?

VERY_VERBOSE - this level is used to record messages about component implementation. There should be very many of 
these and this level is not expected to be useful for logging more then a one component at a time. For example 
messages about changes in queue state during Sent/Receive calls of DMA packets would be at this level. 
Components may generate 1000000s of messages at this level. 

*/

// Logging levels
#define LVL_CRITICAL     1
#define LVL_FATAL        2
#define LVL_ERROR        3
#define LVL_WARNING      4 
#define LVL_INFORMATION  5
#define LVL_VERBOSE      6
#define LVL_VERY_VERBOSE 7

// Standard definition for LOG(COMPID, LEVEL, MSG, ...) logging macro
// In order to provide a custom definition - define CUSTOM_LOGGING_MACRO prior to including this file

#if !defined(DISABLE_LOGGING) && !defined(DEVICE_SIDE_LOGGING)

// Set a define that indicates that WMI logging is enabled
#define ENABLED_WMI_LOGGING 1

// On the desktop, these shouldn't do anything
#define InitializeLogging(str) 
#define ShutdownLogging()


#ifndef CUSTOM_LOGGING_MACRO

#define WPP_CHECK_FOR_NULL_STRING 1
#define WPP_GLOBALLOGGER 1

#define WPP_COMPID_LEVEL_LOGGER(xflags,lvl) (WPP_CONTROL(0).Logger),
#define WPP_COMPID_LEVEL_ENABLED(xflags,lvl) \
   ((WPP_CONTROL(0).Flags[WPP_FLAG_NO(WPP_BIT_ ## xflags)] & WPP_MASK(WPP_BIT_ ## xflags)) && \
    (WPP_CONTROL(0).Level >= lvl))

#define WPP_LEVEL_COMPID_LOGGER(lvl, xflags)  WPP_COMPID_LEVEL_LOGGER(xflags,lvl)
#define WPP_LEVEL_COMPID_ENABLED(lvl, xflags) WPP_COMPID_LEVEL_ENABLED(xflags,lvl)

#endif // CUSTOM_LOGGING_MACRO

#else // !DISABLE_LOGGING && !DEVICE_SIDE_LOGGING

// WMI logging is not turned on
#undef ENABLED_WMI_LOGGING
// Define the clean and init APIs to be nothing
#define WPP_CLEANUP()
#define WPP_COMPID_LEVEL_ENABLED(xflags,lvl) false 
#define WPP_INIT_TRACING(str)

// Device side logging goes here
#if !defined(DISABLE_LOGGING) && defined(DEVICE_SIDE_LOGGING)

#include "logging.h"

#endif // !DISABLE_LOGGING && DEVICE_SIDE_LOGGING

// Define the default logging macros to mean nothing if logging is disabled
#if !defined(CUSTOM_LOGGING_MACRO) && defined(DISABLE_LOGGING)

#pragma warning (disable:4002) // too many arguments to a macro

#define LOG(x) 
#define LOG_FATAL(x) 
#define LOG_ERROR(x) 
#define LOG_WARN(x) 
#define LOG_INFO(x) 
#define LOG_VERBOSE(x) 
#define LOG_VVERBOSE(x) 
#endif

#endif // !DISABLE_LOGGING && !DEVICE_SIDE_LOGGING

#endif // VSD_LOGGING_H__

/*-------------------------- Sample ---------------------------------------------------
Sample flag/control guid definition file:

// Include definition for logging levels
#include "vsd_logging.h"
        
// Define the control guids and flags
#define WPP_CONTROL_GUIDS \
    WPP_DEFINE_CONTROL_GUID(DeviceEmulatorCtlGuid,(F836FFA2,9211,487e,9563,79426ED635A3), \
        WPP_DEFINE_BIT(GENERAL)                \
        WPP_DEFINE_BIT(NETWORK)                \
        WPP_DEFINE_BIT(DMA)                    \
	)               
        
#define WPP_DLL // Uncomment for a DLL component (requires _WIN32_WINNT >= 0x0500)

*/



