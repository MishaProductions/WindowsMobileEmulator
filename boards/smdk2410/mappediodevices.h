/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

Addresses must be in sorted order:  the table is accessed via a binary search.

--*/

// Note: this file is multiply-included, so do not protect with "#pragma once" or "#if"

// addresses taken from map.a
MAPPEDIODEVICE(IOAM29LV800BB ,  CSSROMBank0,          0x00000000, 32*1024*1024) // from map.s's "32 MB SROM(SRAM/ROM) BANK 0"
// from pcmcia.c (PD6710_MEM_BASE_ADDRESS and the 32mb that follows)
MAPPEDIODEVICE(IOMAPPEDMEMPCMCIA,  MAPPEDMEMPCMCIA,    0x10000000,0xFFFF)// Address space for Memory Mapped PCMCIA operations
MAPPEDIODEVICE(IOMAPPEDIOPCMCIA,  MAPPEDIOPCMCIA, 0x10010000, 0xFF03DF) //Address space for IO Mapped PCMCIA operations
MAPPEDIODEVICE(IOCONTROLPCMCIA, CONTROLPCMCIA, 0x110003e0, 0xFFFC20)//Address space for PCMCIA Control class 
MAPPEDIODEVICE(IOCS8900Memory,  CS8900Memory,         0x18000000, 0x15c) // Part of the CS8900 network interface card
MAPPEDIODEVICE(IOCS8900IO,      CS8900IO,             0x19000300, 0x0e) // Part of the CS8900 network interface card
MAPPEDIODEVICE(IOMemoryController, MemoryController,  0x48000000, 0x34) // Incorrectly documented as 0x49000000 in s2410.h
#ifdef DEBUG
MAPPEDIODEVICE(IOUSBHostController, USBHostController,        0x49000000, 0x4096) // USB Host registers - based on Macallan sources
#endif // DEBUG
MAPPEDIODEVICE(IOInterruptController, InterruptController, 0x4a000000, 0x1c)
MAPPEDIODEVICE(IODMAController1,DMAController1,       0x4b000040, 0x3f) // DMA1 is used by audio for input
MAPPEDIODEVICE(IODMAController2,DMAController2,       0x4b000080, 0x3f) // DMA2 is used by audio for output
MAPPEDIODEVICE(IOClockAndPower,	ClockAndPower,		  0x4c000000, 0x18)
MAPPEDIODEVICE(IOLCDController, LCDController,        0x4d000000, 0x60)
MAPPEDIODEVICE(IONANDFlashController, NANDFlashController, 0x4e000000, 0x14)
MAPPEDIODEVICE(IOUART0,			UART0,				  0x50000000, 0x2c) 
MAPPEDIODEVICE(IOUART1,			UART1,				  0x50004000, 0x2c) 
MAPPEDIODEVICE(IOUART2,			UART2,				  0x50008000, 0x2c) 
MAPPEDIODEVICE(IODMATransport,  DMATransport,         0x500f0000, 0x211c) // the UARTs have 1mb of space reserved (0x50000000-0x50100000) so grab a chunk near the top of the reserve
MAPPEDIODEVICE(IOFolderSharing, FolderSharing,        0x500f4000, 0x0c)
MAPPEDIODEVICE(IOEmulServ,      EmulServ,             0x500f5000, 0x08)
MAPPEDIODEVICE(IOPWMTimer,      PWMTimer,             0x51000000, 0x40)
#ifdef DEBUG
MAPPEDIODEVICE(IOUSBDevice,     USBDevice,            0x52000140, 0x12c) // USB device register
#endif // DEBUG
MAPPEDIODEVICE(IOWatchDogTimer, WatchDogTimer,		  0x53000000, 0xc)
// 0x54000000 - IIC control
MAPPEDIODEVICE(IOIIS,			IIS,				  0x55000000, 12)
MAPPEDIODEVICE(IOGPIO,          GPIO,                 0x56000000, 0xc0)
MAPPEDIODEVICE(IORealTimeClock,	RealTimeClock,		  0x57000040, 0x4c)
MAPPEDIODEVICE(IOADConverter,   ADConverter,          0x58000000, 0x10) // A/D Converter is the touch screen
MAPPEDIODEVICE(IOSPI1,          SPI1,                 0x59000020, 0x14) // SPI1 is the PS/2 keyboard
// 0x5a000000 - SD Interface

