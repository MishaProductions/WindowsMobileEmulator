/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/


#include <windows.h>

#include "wincevkkeys.h"

#define ScanCodeTableFirst  0x00
#define ScanCodeTableLast   0x76

static const UINT8 ScanCodeToVKeyTable[] =
{
	0,        //				0x00000000
	VK_ESCAPE,   //				0x00000001
	'1',   //					0x00000002
	'2',   //					0x00000003
	'3',   //					0x00000004
	'4',   //					0x00000005
	'5',   //					0x00000006
	'6',   //					0x00000007
	'7',   //					0x00000008
	'8',   //					0x00000009
	'9',   //					0x0000000a
	'0',   //					0x0000000b
	VK_HYPHEN,    //			0x0000000c
	VK_EQUAL,     //			0x0000000d
	VK_BACK   ,   //			0x0000000e
	VK_TAB,   //				0x0000000f
	'Q',   //					0x00000010
	'W',   //					0x00000011
	'E',   //					0x00000012
	'R',   //					0x00000013
	'T',   //					0x00000014
	'Y',   //					0x00000015
	'U',   //					0x00000016
	'I',   //					0x00000017
	'O',   //					0x00000018
	'P',   //					0x00000019
	VK_LBRACKET,   //			0x0000001a
	VK_RBRACKET,   //			0x0000001b
	VK_RETURN,   //				0x0000001c
	VK_RCONTROL,   //			0x0000001d
	'A',   //					0x0000001e
	'S',   //					0x0000001f
	'D',   //					0x00000020
	'F',   //					0x00000021
	'G',   //					0x00000022
	'H',   //					0x00000023
	'J',   //					0x00000024
	'K',   //					0x00000025
	'L',   //					0x00000026
	VK_SEMICOLON,   //			0x00000027
	VK_BACKQUOTE,   //			0x00000028
	VK_APOSTROPHE,    //		0x00000029
	VK_LSHIFT,   //				0x0000002a
	VK_BACK,   //				0x0000002b
	'Z',   //					0x0000002c
	'X',   //					0x0000002d
	'C',   //					0x0000002e
	'V',   //					0x0000002f
	'B',   //					0x00000030
	'N',   //					0x00000031
	'M',   //					0x00000032
	VK_COMMA,   //				0x00000033
	VK_PERIOD,   //				0x00000034
	VK_DIVIDE,   //				0x00000035
	VK_RSHIFT,   // 			0x00000036
	VK_MULTIPLY,   //			0x00000037
	VK_LMENU,   //				0x00000038
	VK_SPACE,   //				0x00000039
	0,        //				0x0000003a
	VK_F1,   //					0x0000003b
	VK_F2,   //					0x0000003c
	VK_F3,   //					0x0000003d
	VK_F4,   //					0x0000003e
	VK_F5,   //					0x0000003f
	VK_F6,   //					0x00000040
	VK_F7,   //					0x00000041
	VK_F8,   //					0x00000042
	VK_F9,   //					0x00000043
	VK_F10,   //				0x00000044
	VK_NUMLOCK,   //			0x00000045
	0,        //				0x00000046
	VK_NUMPAD7,   //			0x00000047
	VK_UP,        //			0x00000048
	VK_NUMPAD9,   //			0x00000049
	VK_SUBTRACT,   //			0x0000004a
	VK_LEFT,      //			0x0000004b
	VK_NUMPAD5,   //			0x0000004c
	VK_RIGHT,     //			0x0000004d
	VK_ADD,       //			0x0000004e
	VK_NUMPAD1,   //			0x0000004f
	VK_DOWN,       //			0x00000050
	VK_NUMPAD3,   //			0x00000051
	VK_NUMPAD0,   //			0x00000052
	VK_DECIMAL,   //			0x00000053
	0,        //				0x00000054
	0,        //				0x00000055
	0,        //				0x00000056
	VK_F11,   //				0x00000057
	VK_F12,   //				0x00000058
	0,        //				0x00000059
	0,        //				0x0000005a
	VK_LWIN,   //				0x0000005b
	VK_RWIN,   //				0x0000005c
	VK_APPS,   //				0x0000005d
	0,        //				0x0000005e
	0,        //				0x0000005f
	VK_NUMPAD0,//				0x00000060
	VK_NUMPAD1,//				0x00000061
	VK_NUMPAD2,//				0x00000062
	VK_NUMPAD3,//				0x00000063
	VK_NUMPAD4,//				0x00000064
	VK_NUMPAD6,//				0x00000065
	VK_NUMPAD6,//				0x00000066
	VK_NUMPAD7,//				0x00000067
	VK_NUMPAD8,//				0x00000068
	VK_NUMPAD9,//				0x00000069
	VK_MULTIPLY,//				0x0000006A
	VK_ADD     ,//				0x0000006B
	VK_SEPARATOR,//				0x0000006C
	VK_SUBTRACT,//				0x0000006D
	VK_DECIMAL,//				0x0000006E
	VK_DIVIDE, //				0x0000006F
	VK_APP_LAUNCH1,//			0x00000070
	VK_APP_LAUNCH2,//			0x00000071
	VK_APP_LAUNCH3,//			0x00000072
	VK_APP_LAUNCH4,//			0x00000073
	VK_APP_LAUNCH5,//			0x00000074
	EMUL_RESET,    //			0x00000075 // Special CE_PC reset key

/*
	The above array is a one to many mapping. The folowing are the substitutions that can be made:

	VK_LCONTROL,   //			0x0000001d
	VK_RCONTROL,   //			0x0000001d
	VK_EXECUTE,   //			0x0000001c
	VK_SLASH,   //				0x00000035
	VK_RMENU,   //				0x00000038
	VK_LMENU,   //				0x00000038

	VK_HOME,   //				0x00000047
	VK_UP,   //					0x00000048
	VK_NEXT,   //				0x00000049
	VK_LEFT,   //				0x0000004b
	VK_RIGHT,   //				0x0000004d
	VK_DOWN,   //				0x00000050
	VK_INSERT,   //				0x00000052


	VK_DELETE,   //				0x05000053
	VK_END,   //				0x0500004f
	VK_PRIOR,   //				0x05000051

*/
};
