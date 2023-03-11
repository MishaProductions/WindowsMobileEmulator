/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#include "emulator.h"
#include "config.h"
#include "cpu.h"
#include "MappedIO.h"
#include "Devices.h"
#include "resource.h"
#include "PCMCIADevices.h"
#include "config.h"

// Include the logging infrastructure
#include "dev_emulator_log.h"
#define TMH_FILENAME "pcmciadevices.tmh"
#include "vsd_logging_inc.h"

const unsigned __int8 IONE2000::CISData[] = {

    // This data comes from http://www.linkclub.or.jp/~clover/cis/NE2000c.cis
0x01, 3, // Tuple #1, code = 0x1 (Common memory descriptor), length = 3
    0xdc,0x00,0xff,
//    Common memory device information:
//        Device number 1, type Function specific, WPS = ON
//        Speed = 100nS, Memory block size = 512b, 1 units
0x17, 3, // Tuple #2, code = 0x17 (Attribute memory descriptor), length = 3
    0x49,0x00,0xff,
//    Attribute memory device information:
//        Device number 1, type EEPROM, WPS = ON
//        Speed = 250nS, Memory block size = 512b, 1 units
0x21, 2, // Tuple #3, code = 0x21 (Functional ID), length = 2
    0x06,0x03,
//    Network/LAN adapter - POST initialize - Card has ROM
0x15, 27, // Tuple #4, code = 0x15 (Version 1 info), length = 27
    0x04,0x01,0x50,0x43,0x4d,0x43,0x49,0x41,0x00,0x45,0x74,0x68,0x65,0x72,0x6e,0x65,
    0x74,0x20,0x43,0x61,0x72,0x64,0x00,0x00,0x00,0x00,0xff,
//    Version = 4.1, Manuf = [PCMCIA],card vers = [Ethernet Card]
//    Addit. info = [],[]
0x13, 3, // Tuple #5, code = 0x13 (Link target), length = 3
    0x43,0x49,0x53,
0x1a, 5, // Tuple #6, code = 0x1a (Configuration map), length = 5
    0x01,0x24,0xf8,0x03,0x03,
//    Reg len = 2, config register addr = 0x3f8, last config = 0x24
//    Registers: XX------ 
0x1b, 17, // Tuple #7, code = 0x1b (Configuration entry), length = 17
    0xe0,0x81,0x1d,0x3f,0x55,0x4d,0x5d,0x06,0x86,0x46,0x26,0xfc,0x24,0x65,0x30,0xff,
    0xff,
//    Config index = 0x20(default)
//    Interface byte = 0x81 (I/O)  wait signal supported
//    Vcc pwr:
//        Nominal operating supply voltage: 5 x 1V
//        Minimum operating supply voltage: 4.5 x 1V
//        Maximum operating supply voltage: 5.5 x 1V
//        Continuous supply current: 1 x 100mA
//        Max current average over 1 second: 1 x 100mA, ext = 0x46
//        Max current average over 10 ms: 2 x 100mA
//    Wait scale Speed = 1.5 x 10 us
//    Card decodes 4 address lines, 8 Bit I/O only
//        IRQ modes: Level
//        IRQ level = 4
0x1b, 7, // Tuple #8, code = 0x1b (Configuration entry), length = 7
    0x20,0x08,0xca,0x60,0x00,0x03,0x1f,
//    Config index = 0x20
//    Card decodes 10 address lines, limited 8/16 Bit I/O
//        I/O address # 1: block start = 0x300 block length = 0x20
0x1b, 7, // Tuple #9, code = 0x1b (Configuration entry), length = 7
    0x21,0x08,0xca,0x60,0x20,0x03,0x1f,
//    Config index = 0x21
//    Card decodes 10 address lines, limited 8/16 Bit I/O
//        I/O address # 1: block start = 0x320 block length = 0x20
0x1b, 7, // Tuple #10, code = 0x1b (Configuration entry), length = 7
    0x22,0x08,0xca,0x60,0x40,0x03,0x1f,
//    Config index = 0x22
//    Card decodes 10 address lines, limited 8/16 Bit I/O
//        I/O address # 1: block start = 0x340 block length = 0x20
0x1b, 7, // Tuple #11, code = 0x1b (Configuration entry), length = 7
    0x23,0x08,0xca,0x60,0x60,0x03,0x1f,
//    Config index = 0x23
//    Card decodes 10 address lines, limited 8/16 Bit I/O
//        I/O address # 1: block start = 0x360 block length = 0x20
0x20, 4, // Tuple #12, code = 0x20 (Manufacturer ID), length = 4
    0x01,0x8a,0x00,0x01,
//    PCMCIA ID = 0x8a01, OEM ID = 0x100
0x14, 0, // Tuple #13, code = 0x14 (No link), length = 0
0xff, 0  // Tuple #14, code = 0xff (Terminator), length = 0
};

unsigned __int8  __fastcall IONE2000::ReadMemoryByte(unsigned __int32 IOAddress)
{
    if (IOAddress >= RamBase && IOAddress < RamBase+RamSize) {
        return CardRAM[IOAddress-RamBase];
    } else if (IOAddress == 0x3fa) {
        return FCSR.Byte;
    } else if (IOAddress < sizeof(CISData)*2) {
        // ROM is 8-bit but the PCMCIA card is accessed in 16-bit mode.  When this
        // happens, values in ROM are repeated twice.  WinCE handles this by
        // multiplying ROM offsets by 2 before accessing them.
        return CISData[IOAddress/2];
    } else if (IOAddress >= 0x3f8 && IOAddress < 0x3fa) {
        // the SMDK2410's pcmcia.c reads and writes these two bytes, to set the
        // card to 16-bit mode when powered at 5V.  Ignore them.
        return 0;
    } else {
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        return 0;
    }
}

unsigned __int16 __fastcall IONE2000::ReadMemoryHalf(unsigned __int32 IOAddress)
{
    if (IOAddress >= RamBase && IOAddress < RamBase+RamSize) {
        return *(unsigned __int16*)&CardRAM[IOAddress-RamBase];
    } else if (IOAddress < sizeof(CISData)*2) {
        // ROM is 8-bit but the PCMCIA card is accessed in 16-bit mode.  When this
        // happens, values in ROM are repeated twice.  WinCE handles this by
        // multiplying ROM offsets by 2 before accessing them.
        unsigned __int16 Value = CISData[IOAddress/2];
        return (Value << 8) | Value;
    } else {
        // Don't assert here - HwRamTest() probes each 1k region from 0...65536 looking for
        // RAM by writing and reading a test pattern.
        return 0;
    }
}

void __fastcall IONE2000::WriteMemoryByte(unsigned __int32 IOAddress, unsigned __int8  Value)
{
    if (IOAddress >= RamBase && IOAddress < RamBase+RamSize) {
        CardRAM[IOAddress-RamBase] = Value;
    } else if (IOAddress == 0x3fa) {
        FCSR.Byte = Value;
        if (FCSR.Bits.FCSR_INTR_ACK) {
            FCSR.Bits.FCSR_INTR=0;
            FCSR.Bits.FCSR_INTR_ACK=0;
        }
    } else if (IOAddress >= 0x3f8 && IOAddress < 0x3fa) {
        // the SMDK2410's pcmcia.c reads and writes these two bytes, to set the
        // card to 16-bit mode when powered at 5V.  Ignore them.
    } else {
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
    }
}

void __fastcall IONE2000::WriteMemoryHalf(unsigned __int32 IOAddress, unsigned __int16 Value)
{
    if (IOAddress >= RamBase && IOAddress < RamBase+RamSize) {
        *(unsigned __int16*)&CardRAM[IOAddress-RamBase] = Value;
    } else {
        // Don't assert here - HwRamTest() probes each 1k region from 0...65536 looking for
        // RAM by writing and reading a test pattern.
    }
}

unsigned __int8 __fastcall IONE2000::ReadByte(unsigned __int32 IOAddress)
{
    switch (IOAddress) {
    case 0:
        return NIC_COMMAND.Byte;

    case 1:
        switch (NIC_COMMAND.Bits.PageNumber) {
        case 0:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE); // NIC_PAGE_START is write-only
            return 0;
        case 1:
            return NIC_PHYS_ADDR[0];
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            return 0;
        }

    case 2:
        switch (NIC_COMMAND.Bits.PageNumber) {
        case 0:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);    // NIC_PAGE_STOP is write-only
            return 0;
        case 1:
            return NIC_PHYS_ADDR[1];
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            return 0;
        }

    case 3:
        switch (NIC_COMMAND.Bits.PageNumber) {
        case 0:
            return NIC_BOUNDARY;
        case 1:
            return NIC_PHYS_ADDR[2];
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            return 0;
        }

    case 4:
        switch (NIC_COMMAND.Bits.PageNumber) {
        case 0:
            // note that reads return NIC_XMIT_STATUS while writes modify NIC_XMIT_START
            return NIC_XMIT_STATUS.Byte;
        case 1:
            return NIC_PHYS_ADDR[3];
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            return 0;
        }

    case 5:
        switch (NIC_COMMAND.Bits.PageNumber) {
        case 0:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE); // NIC_XMIT_COUNT_LSB is readonly
            return 0;
        case 1:
            return NIC_PHYS_ADDR[4];
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            return 0;
        }

    case 6:
        switch (NIC_COMMAND.Bits.PageNumber) {
        case 0:
            // note that reads return NIC_FIFO while writes modify NIC_XMIT_COUNT_MSB
            return NIC_FIFO;
        case 1:
            return NIC_PHYS_ADDR[5];
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            return 0;
        }

    case 7:
        switch(NIC_COMMAND.Bits.PageNumber) {
        case 0:
            if (NIC_COMMAND.Bits.CR_STOP && NIC_INTR_STATUS.Bits.ISR_RESET == 0) {
                RaiseInterrupt(ISR_RESET_BIT); // satisfy the polling loop in CardInitialize()
            }
            return NIC_INTR_STATUS.Byte;
        case 1:
            return NIC_CURRENT;
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            return 0;
        }

    case 8:
        switch (NIC_COMMAND.Bits.PageNumber) {
        case 0:
            // note that reads return NIC_CRDA_LSB, while writes set NIC_RMT_ADDR_LSB
            return (unsigned __int8)NIC_CRDA;
        case 1:
            return NIC_MC_ADDR[0];
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            return 0;
        }

    case 9:    
        switch (NIC_COMMAND.Bits.PageNumber) {
        case 0:    // NIC_CRDA_MSB
            return (unsigned __int8)(NIC_CRDA >> 8);
        case 1:
            return NIC_MC_ADDR[1];
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            return 0;
        }
        

    case 0xa:
        switch (NIC_COMMAND.Bits.PageNumber) {
        case 0:     // NIC_RMT_COUNT_LSB is write-only
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            return 0;
        case 1:
            return NIC_MC_ADDR[2];
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            return 0;
        }

    case 0xb:
        switch (NIC_COMMAND.Bits.PageNumber) {
        case 0:     // NIC_RMT_COUNT_MSB is write-only
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            return 0;
        case 1:
            return NIC_MC_ADDR[3];
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            return 0;
        }

    case 0xc:
        switch (NIC_COMMAND.Bits.PageNumber) {
        case 0:     // NIC_RMT_COUNT_MSB is write-only
            return NIC_RCV_STATUS.Byte;
        case 1:
            return NIC_MC_ADDR[4];
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            return 0;
        }

    case 0xd:
        switch (NIC_COMMAND.Bits.PageNumber) {
        case 0:
            return NIC_FAE_ERR_CNTR;
        case 1:
            return NIC_MC_ADDR[5];
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            return 0;
        }

    case 0xe:
        switch (NIC_COMMAND.Bits.PageNumber) {
        case 0:
            return NIC_CRC_ERR_CNTR;
        case 1:
            return NIC_MC_ADDR[6];
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            return 0;
        }

    case 0xf:
        switch (NIC_COMMAND.Bits.PageNumber) {
        case 0:
            return NIC_MISSED_CNTR;
        case 1:
            return NIC_MC_ADDR[7];
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            return 0;
        }

    case 0x10: // NIC_RACK_NIC
        if (DMACount) {
            unsigned __int8 Value;

            if (DMAOffset >= RamBase && DMAOffset < RamBase+RamSize) {
                Value=CardRAM[DMAOffset-RamBase];
            } else if (DMAOffset < RomSize) { 
                // In Word-Wide mode, force the low bit in the address to be zero
                Value=CardROM[(NIC_DATA_CONFIG.Bits.DCR_WORD_WIDE) ? (DMAOffset&~1) : DMAOffset];
            } else {
                Value=0;
            }

            unsigned __int16 TransferSize = (NIC_DATA_CONFIG.Bits.DCR_WORD_WIDE) ? 2 : 1;
            DMACount=DMACount-TransferSize;
            DMAOffset=DMAOffset+TransferSize;
            if (DMACount <= 0) {
                DMACount=0;
                RaiseInterrupt(ISR_DMA_DONE_BIT);
            }
            if (DMAOffset >= (NIC_PAGE_STOP<<8)) {
                // Wrap DMA around at NIC_PAGE_START
                DMAOffset = (NIC_PAGE_START<<8) + ((DMAOffset - (NIC_PAGE_STOP<<8)));
            }
            return Value;
        }
        return 0;

    case 0x11: // this is the high byte of NIC_RACK_NIC
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        return 0;

    case 0x1f:    // NIC_RESET
        return 0;

    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        return 0;
    }
}

unsigned __int16 __fastcall IONE2000::ReadHalf(unsigned __int32 IOAddress)
{
    switch (IOAddress) {
    case 0x10: // NIC_RACK_NIC
        if (DMACount) {
            unsigned __int16 Value;

            if (DMAOffset >= RamBase && DMAOffset < RamBase+RamSize) {
                Value=*(unsigned __int16*)&CardRAM[DMAOffset-RamBase];
            } else if (DMAOffset < RomSize) {
                // In Word-Wide mode, force the low bit in the address to be zero
                Value=*(unsigned __int16*)&CardROM[(NIC_DATA_CONFIG.Bits.DCR_WORD_WIDE) ? (DMAOffset&~1) : DMAOffset];
            } else {
                Value=0;
            }
            DMACount=DMACount-2;
            DMAOffset=DMAOffset+2;
            if (DMACount <= 0) {
                DMACount=0;
                RaiseInterrupt(ISR_DMA_DONE_BIT);
            }
            if (DMAOffset >= (NIC_PAGE_STOP<<8)) {
                // Wrap DMA around at NIC_PAGE_START
                DMAOffset = (NIC_PAGE_START<<8) + ((DMAOffset - (NIC_PAGE_STOP<<8)));
            }
            return Value;
        }
        return 0;

    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        return 0;
    }
}


void __fastcall IONE2000::WriteByte(unsigned __int32 IOAddress, unsigned __int8 Value)
{
    switch (IOAddress) {
    case 0:
        {
            NIC_COMMAND_Register OldNIC_COMMAND;

            OldNIC_COMMAND.Byte = NIC_COMMAND.Byte;
            NIC_COMMAND.Byte = Value;

            if (NIC_COMMAND.Bits.CR_START) {
                // The guest wants to start one or more operations

                if (OldNIC_COMMAND.Bits.CR_STOP && NIC_COMMAND.Bits.CR_STOP == 0) {
                    // Exiting the reset state (CR_STOP was previously set, and now
                    // CR_STOP is clear and CR_START is set).  Begin operations.
                    BeginAsyncReceive();
                }

                if (NIC_COMMAND.Bits.CR_NO_DMA) {
                    // Start stopping DMA in progress
                    DMACount = 0;
                }

                if (NIC_COMMAND.Bits.CR_XMIT) {
                    // Transmit the packet that was just DMA'd in.
                    NIC_XMIT_STATUS.Byte=0; // clear the TSR at the beginning of transmission
                    if (NIC_DATA_CONFIG.Bits.DCR_NORMAL && NIC_XMIT_CONFIG.Bits.TCR_NIC_LBK == 0) { // TCR_NIC_LBK is also TCR_LOOPBACK
                        // Transmit a packet
                        unsigned __int16 RMT_ADDR;
                        unsigned __int16 XMIT_COUNT;

                        RMT_ADDR = (unsigned __int16)NIC_XMIT_START*256;
                        XMIT_COUNT = NIC_XMIT_COUNT;
                        if (RMT_ADDR < RamBase || RMT_ADDR+XMIT_COUNT > RamBase+RamSize) {
                            // The packet isn't contained within CardRAM[] - ignore it.
                            ASSERT(FALSE);
                        } else {
                            if (!VPCNet.BeginAsyncTransmitPacket(XMIT_COUNT, &CardRAM[RMT_ADDR-RamBase])) {
                                // Transmission failed
                                ASSERT(FALSE);
                                NIC_COMMAND.Bits.CR_XMIT=0;
                                RaiseInterrupt(ISR_XMIT_ERR_BIT);
                            }
                        }
                    } else {
                        NIC_COMMAND.Bits.CR_XMIT=0;
                        RaiseInterrupt(ISR_XMIT_BIT);
                    }
                }

                if (NIC_COMMAND.Bits.CR_DMA_READ || NIC_COMMAND.Bits.CR_DMA_WRITE) {
                    DMACount = NIC_RMT_COUNT;
                    DMAOffset = NIC_RMT_ADDR;
                }
            } 
            
            if (NIC_COMMAND.Bits.CR_STOP) {
                // Stop the card.
                NIC_INTR_STATUS.Bits.ISR_RESET=0;
                RaiseInterrupt(ISR_RESET_BIT);
            }
        }
        break;

    case 1:
        switch (NIC_COMMAND.Bits.PageNumber) {
        case 0:
            NIC_PAGE_START=Value;
            break;
        case 1:
            NIC_PHYS_ADDR[0]=Value;
            break;
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            break;
        }
        break;

    case 2:
        switch (NIC_COMMAND.Bits.PageNumber) {
        case 0:
            NIC_PAGE_STOP=Value;
            break;
        case 1:
            NIC_PHYS_ADDR[1]=Value;
            break;
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            break;
        }
        break;

    case 3:
        switch (NIC_COMMAND.Bits.PageNumber) {
        case 0:
            NIC_INTR_STATUS.Bits.ISR_RESET=0; // cleared when one or more packets have been removed from the receive buffer
            NIC_BOUNDARY=Value;
            break;
        case 1:
            NIC_PHYS_ADDR[2]=Value;
            break;
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            break;
        }
        break;

    case 4:
        switch (NIC_COMMAND.Bits.PageNumber) {
        case 0:
            // This holds the start offset of the packet to transmit.  The offset is specified
            // in terms of the number of 256-byte buffers, so << by 8 to convert back into a
            // byte offset.
            NIC_XMIT_START=Value;
            break;
        case 1:
            NIC_PHYS_ADDR[3]=Value;
            break;
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            break;
        }
        break;

    case 5: 
        switch (NIC_COMMAND.Bits.PageNumber) {
        case 0: // NIC_XMIT_COUNT_LSB
            NIC_XMIT_COUNT = (NIC_XMIT_COUNT & 0xff00) | Value;
            break;
        case 1:
            NIC_PHYS_ADDR[4]=Value;
            break;
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            break;
        }
        break;

    case 6: 
        switch (NIC_COMMAND.Bits.PageNumber) {
        case 0:
            // note that reads return NIC_FIFO while writes modify NIC_XMIT_COUNT_MSB
            NIC_XMIT_COUNT = (NIC_XMIT_COUNT & 0xff) | ((unsigned __int16)Value << 8);
            break;
        case 1:
            NIC_PHYS_ADDR[5]=Value;
            break;
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            break;
        }
        break;

    case 7:
        switch (NIC_COMMAND.Bits.PageNumber) {
        case 0:
            NIC_INTR_STATUS.Byte &= ~Value;    // ack interrupts
            ClearInterrupt();
            break;
        case 1:
            NIC_CURRENT=Value;
            break;
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            break;
        }
        break;

    case 8:
        switch (NIC_COMMAND.Bits.PageNumber) {
        case 0:
            // note that reads return NIC_CRDA_LSB, while writes set NIC_RMT_ADDR_LSB
            NIC_RMT_ADDR=(NIC_RMT_ADDR & 0xff00) | Value;
            break;
        case 1:
            NIC_MC_ADDR[0]=Value;
            break;
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            break;
        }
        break;

    case 9:
        switch (NIC_COMMAND.Bits.PageNumber) {
        case 0:
            // note that reads return NIC_CRDA_MSB, while writes set NIC_RMT_ADDR_MSB
            NIC_RMT_ADDR = (NIC_RMT_ADDR & 0xff) | ((unsigned __int16)Value << 8);
            break;
        case 1:
            NIC_MC_ADDR[1]=Value;
            break;
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            break;
        }
        break;

    case 0xa: 
        switch (NIC_COMMAND.Bits.PageNumber) {
        case 0: // NIC_RMT_COUNT_LSB
            NIC_RMT_COUNT = (NIC_RMT_COUNT & 0xff00) | Value;
            break;
        case 1:
            NIC_MC_ADDR[2]=Value;
            break;
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            break;
        }
        break;

    case 0xb:
        switch (NIC_COMMAND.Bits.PageNumber) {
        case 0: // NIC_RMT_COUNT_MSB
            NIC_RMT_COUNT = (NIC_RMT_COUNT & 0xff) | ((unsigned __int16)Value << 8);
            break;
        case 1:
            NIC_MC_ADDR[3]=Value;
            break;
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            break;
        }
        break;

    case 0xc:
        switch (NIC_COMMAND.Bits.PageNumber) {
        case 0: // NIC_RCV_CONFIG
            if (Value != NIC_RCV_CONFIG.Byte) {
                int FilterValue=VPCNet.PACKET_TYPE_DIRECTED;

                NIC_RCV_CONFIG.Byte = Value;

                if (NIC_RCV_CONFIG.Bits.RCR_BROADCAST) {
                    FilterValue |= VPCNet.PACKET_TYPE_BROADCAST;
                }
                if (NIC_RCV_CONFIG.Bits.RCR_ALL_PHYS) {
                    FilterValue |= VPCNet.PACKET_TYPE_PROMISCUOUS;
                }
                if (NIC_RCV_CONFIG.Bits.RCR_MULTICAST) {
                    FilterValue |= VPCNet.PACKET_TYPE_MULTICAST;
                }
                if (!VPCNet.ConfigurePacketFilter(FilterValue)) {
                    ASSERT(FALSE);
                }
            }
            break;
        case 1:
            NIC_MC_ADDR[4]=Value;
            break;
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            break;
        }
        break;

    case 0xd:
        switch (NIC_COMMAND.Bits.PageNumber) {
        case 0: 
            NIC_XMIT_CONFIG.Byte = Value;
            break;
        case 1:
            NIC_MC_ADDR[5]=Value;
            break;
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            break;
        }
        break;

    case 0xe:
        switch (NIC_COMMAND.Bits.PageNumber) {
        case 0: 
            NIC_DATA_CONFIG.Byte = Value;
            break;
        case 1:
            NIC_MC_ADDR[6]=Value;
            break;
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            break;
        }
        break;

    case 0xf:
        switch (NIC_COMMAND.Bits.PageNumber) {
        case 0: 
            NIC_INTR_MASK.Byte = Value;
            if (NIC_INTR_MASK.Byte & NIC_INTR_STATUS.Byte) {
                // Some interrupts have been unmasked
                CONTROLPCMCIA.RaiseCardIRQ();
            } else if ((NIC_INTR_MASK.Byte & NIC_INTR_STATUS.Byte) == 0) {
                // All interrupts have been masked
                CONTROLPCMCIA.ClearCardIRQ();
            }
            break;
        case 1:
            NIC_MC_ADDR[7]=Value;
            break;
        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            break;
        }
        break;

    case 0x10: // NIC_RACK_NIC
        if (DMACount) {
            if (DMAOffset >= RamBase && DMAOffset < RamBase+RamSize) {
                CardRAM[DMAOffset-RamBase] = Value;
            } else {
                // nothing to do
            }
            unsigned __int16 TransferSize = (NIC_DATA_CONFIG.Bits.DCR_WORD_WIDE) ? 2 : 1;
            DMACount=DMACount-TransferSize;
            DMAOffset=DMAOffset+TransferSize;
            if (DMACount <= 0) {
                DMACount=0;
                RaiseInterrupt(ISR_DMA_DONE_BIT);
            }
            if (DMAOffset >= (NIC_PAGE_STOP<<8)) {
                // Wrap DMA around at NIC_PAGE_START
                DMAOffset = (NIC_PAGE_START<<8) + ((DMAOffset - (NIC_PAGE_STOP<<8)));
            }
        }
        break;

    case 0x11:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        break;

    case 0x1f: // NIC_RESET
        NIC_COMMAND.Byte=0;
        NIC_COMMAND.Bits.CR_STOP=1;
        NIC_PAGE_START=0;
        NIC_PHYS_ADDR[0]=0;
        NIC_PHYS_ADDR[1]=0;
        NIC_PHYS_ADDR[2]=0;
        NIC_PHYS_ADDR[3]=0;
        NIC_PHYS_ADDR[4]=0;
        NIC_PHYS_ADDR[5]=0;
        NIC_PAGE_STOP=0;
        NIC_BOUNDARY=0;
        NIC_XMIT_STATUS.Byte=0;
        NIC_XMIT_START=0;
        NIC_XMIT_COUNT=0;
        NIC_FIFO=0;
        NIC_INTR_STATUS.Byte=0;
        NIC_INTR_STATUS.Bits.ISR_RESET=1;
        NIC_CURRENT=0;
        NIC_CRDA=0;
        NIC_RMT_ADDR=0;
        NIC_RMT_COUNT=0;
        NIC_RCV_CONFIG.Byte=0;
        NIC_RCV_STATUS.Byte=0;
        NIC_XMIT_CONFIG.Byte=0;
        NIC_XMIT_CONFIG.Bits.TCR_NIC_LBK=1;
        NIC_XMIT_CONFIG.Bits.TCR_SNI_LBK=1;
        NIC_FAE_ERR_CNTR=0;
        NIC_DATA_CONFIG.Byte=0;
        NIC_CRC_ERR_CNTR=0;
        memset(CardRAM, 0, sizeof(CardRAM));
        break;


    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
    }
}

void __fastcall IONE2000::WriteHalf(unsigned __int32 IOAddress, unsigned __int16 Value)
{
    switch (IOAddress) {
    case 0x10: // NIC_RACK_NIC
        if (DMACount) {
            if (DMAOffset >= RamBase && DMAOffset < RamBase+RamSize) {
                DMAOffset&=~1;
                *(unsigned __int16*)&CardRAM[DMAOffset-RamBase] = Value;
            } else {
                // nothing to do
            }
            if (DMACount <= 2) {
                DMACount=0;
                RaiseInterrupt(ISR_DMA_DONE_BIT);
            } else {
                DMACount-=2;
                DMAOffset+=2;
                if (DMAOffset >= (NIC_PAGE_STOP<<8)) {
                    // Wrap DMA around at NIC_PAGE_START
                    DMAOffset = (NIC_PAGE_START<<8) + ((DMAOffset - (NIC_PAGE_STOP<<8)));
                }
            }
        }
        break;

    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        break;
    }
}

inline bool __fastcall IONE2000::ShouldIndicatePacketToGuest(const EthernetHeader & inEthernetHeader)
{
    // Make sure we are in perfect filtering mode.
    ASSERT(inEthernetHeader.fDestinationAddress.IsMulticast());

    if (NIC_RCV_CONFIG.Bits.RCR_BROADCAST && inEthernetHeader.fDestinationAddress.IsBroadcast())    // Is this a broadcast packet?            
       return true;
    
    // Calculate Hash Table position (adapted from Appendix C of DEC 21041 Hardware Reference Manual, p. C-1)
    unsigned __int32 crc = 0xffffffff;
 
    for (unsigned __int32 addressCount = 0; addressCount < 6; addressCount++)
    {
        unsigned __int8 dataByte = inEthernetHeader.fDestinationAddress[addressCount];
        for (unsigned __int32 bitNum = 0; bitNum < 8; bitNum++,dataByte >>= 1)
        {
            if (((crc ^ dataByte) & 0x01) != 0)
                crc = (crc >> 1) ^ kCRC32_Poly;
            else
                crc >>= 1;
        }
    }
    crc &= 0x1ff;
    return (( ( (NIC_MC_ADDR[crc >> 3]) >> (crc & 7) ) & 0x01) != 0);
}

bool __fastcall IONE2000::PowerOn(void)
{
    WriteByte(0x1f, 0xff);  // reset the card by writing to NIC_RESET

    // The NE2000 is powered on and off several times during device detection.  Do any
    // expensive work only on the first PowerOn() call.
    if (fHasBeenPoweredOnBefore) {
        return true;
    }

    TransmitCallback.lpRoutine = TransmitCompletionRoutineStatic;
    TransmitCallback.lpParameter = this;
    ReceiveCallback.lpRoutine = ReceiveCompletionRoutineStatic;
    ReceiveCallback.lpParameter = this;

    // Power on the VPC driver, and initialize the MAC address stored in the NIC
    if (!VPCNet.PowerOn(Configuration.NE2000MACAddress, 
                        L"NE2000", 
                        (unsigned __int8 *)&Configuration.SuggestedAdapterMacAddressNE2000,
                        &TransmitCallback,
                        &ReceiveCallback)) {
        return false;
    }
    VPCNet.ConfigurePacketFilter(VPCNet.PACKET_TYPE_DIRECTED);

    // Copy the MAC address into CardROM.  Each byte of the address is stored in a
    // halfword in CardROM.
    for (int i=0; i<6; ++i) {
        CardROM[i*2] = ((unsigned __int8*)Configuration.NE2000MACAddress)[i];
    }

    // Set up 'WW' in CardROM so code like CardSlotTest() can detect 8-bit vs 16-bit
    // cards.  We want to be a 16-bit card.
    CardROM[14] = CardROM[15] = 'W';

    fHasBeenPoweredOnBefore=true;
    return true;
}

void IONE2000::PowerOff(void)
{
    // PowerOff just flags that the device is powered off. 
    NIC_COMMAND.Byte = 0;
    NIC_COMMAND.Bits.CR_STOP=1;
}

void IONE2000::BeginAsyncReceive(void)
{
    ASSERT_CRITSEC_OWNED(IOLock);

    if (AsyncReceiveCount == 0) {
        AsyncReceiveCount=1;
        if (VPCNet.BeginAsyncReceivePacket(ReceiveBuffer, sizeof(ReceiveBuffer)) == false) {
            ASSERT(FALSE);
        }
    } else {
        ASSERT(AsyncReceiveCount == 1);
    }
}

void IONE2000::ReceiveCompletionRoutineStatic(LPVOID lpParameter, DWORD dwBytesTransferred, LPOVERLAPPED lpOverlapped)
{
    IONE2000 *pThis = (IONE2000*)lpParameter;

    pThis->ReceiveCompletionRoutine(dwBytesTransferred);
}

void IONE2000::ReceiveCompletionRoutine(DWORD dwBytesTransferred)
{
    EnterCriticalSection(&IOLock);
    ASSERT(AsyncReceiveCount == 1);
    AsyncReceiveCount=0;

    if (NIC_COMMAND.Bits.CR_STOP) {
        // The main thread wishes to stop the NE2000
        LeaveCriticalSection(&IOLock);
        return;
    }

    if (!NIC_DATA_CONFIG.Bits.DCR_NORMAL) {
        // loopback mode is enabled - silently drop the packet
        goto Done;
    }
    if (NIC_RCV_CONFIG.Bits.RCR_MONITOR) {
        // Monitor mode is enabled - silently drop the packet
        goto Done;
    }

    // Filter multicast packets so that only the packets that match the address hash
    // are delivered to the guest 
    if (NIC_RCV_CONFIG.Bits.RCR_MULTICAST) {
        // Note:  Technically, we should read the recipient MAC address
        //        and check if it is a multicast address.  If it is, we 
        //        should set CardRAM[(PCUR<<8)+0-RamBase] to RSR_MULTICAST
        //        in addition to RSR_PACKET_OK.  However, the WinCE driver
        //        never examines this bit, so there is no need to compute
        //        it here.

        EthernetHeader & enetDatagram = *reinterpret_cast<EthernetHeader *>(ReceiveBuffer);
        if (enetDatagram.fDestinationAddress.IsMulticast() && !ShouldIndicatePacketToGuest(enetDatagram)) {
            goto Done;
        }
    }

    // Copy the packet into CardRAM
    // NIC_CURRENT<<8 is the byte offset to begin the copy at.  Once 
    // NIC_CURRENT==NIC_PAGE_STOP, it wraps around to NIC_PAGE_START.
    // The NIC writes a 4-byte header, followed by the ethernet packet
    // itself.
    //
    // The IOLock isn't held during the copy - on the NE2000, the
    // transfer is done via DMA concurrently with the CPU running.
    // However, NIC_CURRENT, NIC_PAGE_START, and NIC_PAGE_STOP are all
    // writable registers so the values could change underneath us.
    // Cache local copies and range-check them carefully before
    // using them.
    unsigned __int8 NextPage;
    unsigned __int8 PSTART = NIC_PAGE_START;
    unsigned __int8 PSTOP = NIC_PAGE_STOP;
    unsigned __int8 PCUR = NIC_CURRENT;
    unsigned __int8 BRNY = NIC_BOUNDARY;

    if (PSTART < (RamBase>>8) || PSTART >= ((RamBase+RamSize)>>8) ||
        PSTOP  < (RamBase>>8) || PSTOP  > ((RamBase+RamSize)>>8) &&
        PCUR   < (RamBase>>8) || PCUR   >= ((RamBase+RamSize)>>8)) {
        // One or more of the start/stop/current values do not point
        // into CardRAM.  Ignore the packet.
        goto Done;
    }

    LeaveCriticalSection(&IOLock);
    // Make the expensive memcpy() calls outside of the IOLock

    // Compute the next starting page.  The "+4" accounts for the 4-byte
    // header added by the NE2000, and the total length is rounded up to
    // the next 256-byte page.
    NextPage = PCUR + (unsigned __int8)((dwBytesTransferred+4)>>8) + 1;

    if (NextPage > PSTOP) {
        // The packet wraps around within the circular buffer
        NextPage = PSTART + (NextPage - PSTOP);

        if (BRNY > PCUR || NextPage > PCUR) {
            EnterCriticalSection(&IOLock);
            RaiseInterrupt(ISR_OVERFLOW_BIT);
            goto Done;
        }
        memcpy(&CardRAM[(PCUR<<8)+4-RamBase], 
                ReceiveBuffer, 
                ((PSTOP-PCUR)<<8)-4);
        memcpy(&CardRAM[(PSTART<<8)-RamBase], 
                &ReceiveBuffer[(PSTOP-PCUR)<<8]-4, 
                dwBytesTransferred - (((PSTOP-PCUR)<<8)-4));
    } else {
        // The packet is contiguous
        if (PCUR < BRNY && BRNY < NextPage) {
            EnterCriticalSection(&IOLock);
            RaiseInterrupt(ISR_OVERFLOW_BIT);
            RaiseInterrupt(ISR_RESET_BIT); // see the ISR documentation
            goto Done;
        }
        memcpy(&CardRAM[(PCUR<<8)+4-RamBase], ReceiveBuffer, dwBytesTransferred);
    }
    dwBytesTransferred+=4; // account for the 4-byte NIC header
    CardRAM[(PCUR<<8)+0-RamBase]=1; // RSR_PACKET_OK
    CardRAM[(PCUR<<8)+1-RamBase]=NextPage;
    CardRAM[(PCUR<<8)+2-RamBase]=(unsigned __int8)dwBytesTransferred;
    CardRAM[(PCUR<<8)+3-RamBase]=(unsigned __int8)(dwBytesTransferred >> 8);

    EnterCriticalSection(&IOLock);

    if (NIC_COMMAND.Bits.CR_STOP) {
        LeaveCriticalSection(&IOLock);
        return;
    }

    NIC_CURRENT = NextPage;
    if (NIC_CURRENT >= NIC_PAGE_STOP) {
        // NIC_CURRENT needs to wrap around
        NIC_CURRENT = NIC_PAGE_START + (NIC_CURRENT-NIC_PAGE_STOP);
    }

    NIC_RCV_STATUS.Bits.RSR_MULTICAST=0;
    NIC_RCV_STATUS.Bits.RSR_PACKET_OK=1;
    RaiseInterrupt(ISR_RCV_BIT);

Done:
    if (NIC_COMMAND.Bits.CR_STOP == 0 && NIC_COMMAND.Bits.CR_START == 1) {
        // Begin another async packet receive
        BeginAsyncReceive();
    }

    LeaveCriticalSection(&IOLock);
}


void IONE2000::TransmitCompletionRoutineStatic(LPVOID lpParameter, DWORD dwBytesTransferred, LPOVERLAPPED lpOverlapped)
{
    IONE2000 *pThis = (IONE2000*)lpParameter;

    pThis->TransmitCompletionRoutine();
}

void IONE2000::TransmitCompletionRoutine()
{
    EnterCriticalSection(&IOLock);
    if (NIC_COMMAND.Bits.CR_STOP == 0) {
        NIC_COMMAND.Bits.CR_XMIT=0;
        RaiseInterrupt(ISR_XMIT_BIT);
    }
    LeaveCriticalSection(&IOLock);
}

void __fastcall IONE2000::RaiseInterrupt(unsigned __int8 InterruptType)
{
    ASSERT_CRITSEC_OWNED(IOLock);

    NIC_INTR_STATUS.Byte |= InterruptType;
    FCSR.Bits.FCSR_INTR=1;
    if (InterruptType != 0x80 && (NIC_INTR_MASK.Byte & InterruptType)) {
        // The interrupt is unmasked - raise it now
        CONTROLPCMCIA.RaiseCardIRQ();
    }
}

void __fastcall IONE2000::ClearInterrupt(void)
{
    if (NIC_INTR_STATUS.Byte & NIC_INTR_MASK.Byte) {
        // There are still pending interrupts
        return;
    }
    // Else clear the interrupt
    CONTROLPCMCIA.ClearCardIRQ();
}
