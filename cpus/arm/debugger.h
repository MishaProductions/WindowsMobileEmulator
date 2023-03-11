/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#pragma once
#pragma pack(push)
#pragma pack(8)
#pragma warning(disable:4200 4201)

typedef unsigned __int32 REG_ID_TYPE;
typedef unsigned __int64 REG_DATA_TYPE;
typedef unsigned __int32 ADDRESS_T;
typedef unsigned __int32 SIZE_TYPE;
typedef unsigned __int32 BP_HANDLE_TYPE;

typedef struct _ARM_CONTEXT
{
	union
	{
		struct
		{
			REG_DATA_TYPE r0;
			REG_DATA_TYPE r1;
			REG_DATA_TYPE r2;
			REG_DATA_TYPE r3;
			REG_DATA_TYPE r4;
			REG_DATA_TYPE r5;
			REG_DATA_TYPE r6;
			REG_DATA_TYPE r7;
			REG_DATA_TYPE r8;
			REG_DATA_TYPE r9;
			REG_DATA_TYPE r10;
			REG_DATA_TYPE r11;
			REG_DATA_TYPE r12;
			REG_DATA_TYPE sp;
			REG_DATA_TYPE lr;
			REG_DATA_TYPE pc;
		};
		REG_DATA_TYPE r[32];
	};
	REG_DATA_TYPE cpsr;
	REG_DATA_TYPE spsr;
}
ARM_CONTEXT;

typedef enum _DEBUG_REQUEST_TYPE
{
	drtContinueExecution,
	drtReadContext,
	drtWriteContext,
	drtReadVirtualMemory,
	drtWriteVirtualMemory,
	drtReadPhysicalMemory,
	drtWritePhysicalMemory,
	drtSetBreakpoint,
	drtClearBreakpoint,
	drtSetWatchpoint,
	drtClearWatchpoint
}
DEBUG_REQUEST_TYPE;

typedef enum _DEBUG_NOTIFICATION_TYPE
{
	dntBreakpoint,
	dntException
}
DEBUG_NOTIFICATION_TYPE;

typedef struct _DEBUG_REQUEST_PACKET
{
	union
	{
		struct _COMMON
		{
			size_t size;
			DEBUG_REQUEST_TYPE type;
		}
		COMMON;

		struct _CONT
		{
			struct _COMMON common;
		}
		CONT;
		
		struct _RC
		{
			struct _COMMON common;
		}
		RC;
		
		struct _WC
		{
			struct _COMMON common;
			ARM_CONTEXT context;
		}
		WC;

		struct _RVM
		{
			struct _COMMON common;
			ADDRESS_T addr;
			SIZE_TYPE size;
		}
		RVM;

		struct _WVM
		{
			struct _COMMON common;
			ADDRESS_T addr;
			SIZE_TYPE size;
			BYTE data[0];
		}
		WVM;

		struct _RPM
		{
			struct _COMMON common;
			ADDRESS_T addr;
			SIZE_TYPE size;
		}
		RPM;

		struct _WPM
		{
			struct _COMMON common;
			ADDRESS_T addr;
			SIZE_TYPE size;
			BYTE data[0];
		}
		WPM;

		struct _SB
		{
			struct _COMMON common;
			ADDRESS_T addr;
		}
		SB;

		struct _CB
		{
			struct _COMMON common;
			BP_HANDLE_TYPE handle;
		}
		CB;

		struct _SW
		{
			struct _COMMON common;
			ADDRESS_T addr;
		}
		SW;

		struct _CW
		{
			struct _COMMON common;
			BP_HANDLE_TYPE handle;
		}
		CW;
	};
}
DEBUG_REQUEST_PACKET;

typedef struct _DEBUG_NOTIFICATION_PACKET
{
	union
	{
		struct _COMMON
		{
			size_t size;
			DEBUG_NOTIFICATION_TYPE type;
		}
		COMMON;

		struct _BP
		{
			struct _COMMON common;
			ADDRESS_T addr;
			BP_HANDLE_TYPE handle;
		}
		BP;

		struct _EX
		{
			struct _COMMON common;
			ADDRESS_T addr;
			DWORD status;
		}
		EX;
	};
}
DEBUG_NOTIFICATION_PACKET;

typedef struct _DEBUG_RESPONSE_PACKET
{
	union
	{
		struct _COMMON
		{
			size_t size;
			DEBUG_REQUEST_TYPE type;
			HRESULT result;
		}
		COMMON;
		
		struct _RC
		{
			struct _COMMON common;
			ARM_CONTEXT context;
		}
		RC;
		
		struct _WC
		{
			struct _COMMON common;
		}
		WC;
		
		struct _RVM
		{
			struct _COMMON common;
			SIZE_TYPE size;
			BYTE data[0];
		}
		RVM;

		struct _WVM
		{
			struct _COMMON common;
			SIZE_TYPE size;
		}
		WVM;

		struct _RPM
		{
			struct _COMMON common;
			SIZE_TYPE size;
			BYTE data[0];
		}
		RPM;

		struct _WPM
		{
			struct _COMMON common;
			SIZE_TYPE size;
		}
		WPM;

		struct _SB
		{
			struct _COMMON common;
			BP_HANDLE_TYPE handle;
		}
		SB;
		
		struct _CB
		{
			struct _COMMON common;
		}
		CB;
		
		struct _SW
		{
			struct _COMMON common;
			BP_HANDLE_TYPE handle;
		}
		SW;
		
		struct _CW
		{
		}
		CW;
	};
}
DEBUG_RESPONSE_PACKET;

#define respsizeof(_m) (sizeof(((DEBUG_RESPONSE_PACKET*)0)->_m))
#define reqsizeof(_m) (sizeof(((DEBUG_REQUEST_PACKET*)0)->_m))

#pragma pack(pop)
