/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#include "CompletionPort.h"
#include "vpcnet.h"
#include "WinController.h"
#include "WinInterface.h"
#include "COMInterface.h"
#include "FolderSharing.h"
#include "EmulServ.h"

class IOInterruptController: public MappedIODevice {
public:
    IOInterruptController();

    virtual void __fastcall SaveState(StateFiler& filer) const;
    virtual void __fastcall RestoreState(StateFiler& filer);

    virtual unsigned __int32 __fastcall ReadWord(unsigned __int32 IOAddress);
    virtual void __fastcall WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value);

    typedef enum {
        SourceEINT0      =0x00000001,
        SourceEINT1      =0x00000002,
        SourceEINT2      =0x00000004,
        SourceEINT3      =0x00000008,
        SourceEINT4_7    =0x00000010,
        SourceEINT8_23   =0x00000020,
        SourceReserved1  =0x00000040,
        SourcenBATT_FLT  =0x00000080,
        SourceINT_TICK   =0x00000100,
        SourceINT_WDT    =0x00000200,
        SourceINT_TIMER0 =0x00000400,
        SourceINT_TIMER1 =0x00000800,
        SourceINT_TIMER2 =0x00001000,
        SourceINT_TIMER3 =0x00002000,
        SourceINT_TIMER4 =0x00004000,
        SourceINT_UART2  =0x00008000,
        SourceINT_LCD    =0x00010000,
        SourceINT_DMA0   =0x00020000,
        SourceINT_DMA1   =0x00040000,
        SourceINT_DMA2   =0x00080000,
        SourceINT_DMA3   =0x00100000,
        SourceINT_SDI    =0x00200000,
        SourceINT_SPI0   =0x00400000,
        SourceINT_UART1  =0x00800000,
        SourceReserved2  =0x01000000,
        SourceINT_USBD   =0x02000000,
        SourceINT_USBH   =0x04000000,
        SourceINT_IIC    =0x08000000,
        SourceINT_UART0  =0x10000000,
        SourceINT_SPI1   =0x20000000,
        SourceINT_RTC    =0x40000000,
        SourceINT_ADC    =0x80000000,
        Invalid          =0
    } InterruptSource;
    void __fastcall RaiseInterrupt(InterruptSource Source);

    typedef enum {
        SubSourceINT_RXD0=0x0001,
        SubSourceINT_TXD0=0x0002,
        SubSourceINT_ERR0=0x0004,
        SubSourceINT_RXD1=0x0008,
        SubSourceINT_TXD1=0x0010,
        SubSourceINT_ERR1=0x0020,
        SubSourceINT_RXD2=0x0040,
        SubSourceINT_TXD2=0x0080,
        SubSourceINT_ERR2=0x0100,
        SubSourceINT_TC  =0x0200,
        SubSourceINT_ADC =0x0400
    } InterruptSubSource;
    void __fastcall RaiseInterrupt(InterruptSubSource Source);

    void __fastcall SetInterruptPending();
    void __fastcall SetSubInterruptPending(bool fMayClearSRCPND);
    bool __fastcall IsPending(unsigned __int32 mask) { return (INTPND.Word & mask || SRCPND.Word & mask); }
private:
    typedef union {
        unsigned __int32 Word;
        struct {
            unsigned __int32 EINT0:1;
            unsigned __int32 EINT1:1;
            unsigned __int32 EINT2:1;
            unsigned __int32 EINT3:1;
            unsigned __int32 EINT4_7:1;
            unsigned __int32 EINT8_23:1;
            unsigned __int32 Reserved1:1;
            unsigned __int32 nBATT_FLT:1;
            unsigned __int32 INT_TICK:1;
            unsigned __int32 INT_WDT:1;
            unsigned __int32 INT_TIMER0:1;
            unsigned __int32 INT_TIMER1:1;
            unsigned __int32 INT_TIMER2:1;
            unsigned __int32 INT_TIMER3:1;
            unsigned __int32 INT_TIMER4:1;
            unsigned __int32 INT_UART2:1;
            unsigned __int32 INT_LCD:1;
            unsigned __int32 INT_DMA0:1;
            unsigned __int32 INT_DMA1:1;
            unsigned __int32 INT_DMA2:1;
            unsigned __int32 INT_DMA3:1;
            unsigned __int32 INT_SDI:1;
            unsigned __int32 INT_SPI0:1;
            unsigned __int32 INT_UART1:1;
            unsigned __int32 Reserved2:1;
            unsigned __int32 INT_USBD:1;
            unsigned __int32 INT_USBH:1;
            unsigned __int32 INT_IIC:1;
            unsigned __int32 INT_UART0:1;
            unsigned __int32 INT_SPI1:1;
            unsigned __int32 INT_RTC:1;
            unsigned __int32 INT_ADC:1;
        } Bits;
    } InterruptCollection;

    typedef union {
        unsigned __int16 HalfWord;
        struct {
            unsigned __int16 INT_RXD0:1;
            unsigned __int16 INT_TXD0:1;
            unsigned __int16 INT_ERR0:1;
            unsigned __int16 INT_RXD1:1;
            unsigned __int16 INT_TXD1:1;
            unsigned __int16 INT_ERR1:1;
            unsigned __int16 INT_RXD2:1;
            unsigned __int16 INT_TXD2:1;
            unsigned __int16 INT_ERR2:1;
            unsigned __int16 INT_TC:1;   // Touch interrupt
            unsigned __int16 INT_ADC:1;
            unsigned __int16 Reserved1:5;
        } Bits;
    } SubInterruptCollection;

    static const unsigned __int32 ArbiterPriorities[4];
    static const InterruptSource  ArbiterInputs[6][6];

    // Source Pending (SRCPND).  Bit field for 32 interrupts, where each bit
    // is set by an interrupt source.  Bits are cleared by writing a 1 bit
    // from software.
    InterruptCollection SRCPND;

    // Interrupt Mode (INTMOD).  If a bit is set to 1, then the interrupt is
    // delivered as an FIQ.  Otherwise, it is delivered as an IRQ.
    InterruptCollection INTMOD;

    // Interrupt Mask (INTMASK).
    InterruptCollection INTMASK;

    // Interrupt Pending (INTPND).  At most 1 bit can be set - this reports
    // the highest-priority interrupt pending, and is used by the ISR to determine
    // which interrupt to service.  It is used only for IRQ interrupts.
    InterruptCollection INTPND;

    // IRQ Priority Control (PRIORITY).
    union {
        unsigned __int32 Word;
        struct {
            unsigned __int32 ARB_MODE0:1;
            unsigned __int32 ARB_MODE1:1;
            unsigned __int32 ARB_MODE2:1;
            unsigned __int32 ARB_MODE3:1;
            unsigned __int32 ARB_MODE4:1;
            unsigned __int32 ARB_MODE5:1;
            unsigned __int32 ARB_MODE6:1;
            unsigned __int32 ARB_SEL0:2;
            unsigned __int32 ARB_SEL1:2;
            unsigned __int32 ARB_SEL2:2;
            unsigned __int32 ARB_SEL3:2;
            unsigned __int32 ARB_SEL4:2;
            unsigned __int32 ARB_SEL5:2;
            unsigned __int32 ARB_SEL6:2;
            unsigned __int32 Reserved1:11;
        } Bits;
    } PRIORITY;

    // Interrupt Offset (INTOFFSET).  It reports the bit number set in
    // the INTPND (ie. if INT_ADC is set in INTPND, this register contains 
    // 31).
    unsigned __int32 INTOFFSET;

    // Sub Source Interrupt Pending. (SUBSRCPND).  Sub Interrupts are rolled up into
    // UART0/1/2 in the main interrupt register.
    SubInterruptCollection SUBSRCPND;

    // Sub Interrupt Mask (INTSUBMSK)
    SubInterruptCollection INTSUBMSK;
};


// Handles physical addresses 0x10000400 to 0x10000408

#ifdef AUDIO_TESTING
typedef struct {
  // Riff Header
  unsigned char       groupID[8];
  char       riffTypeID[4];
  // Format chunk
  char        chunkID[4];
  long           chunkSize;
  short          wFormatTag;
  unsigned short wChannels;
  unsigned long  dwSamplesPerSec;
  unsigned long  dwAvgBytesPerSec;
  unsigned short wBlockAlign;
  unsigned short wBitsPerSample;
  // Data chunk
  char        dataID[4];
  long           dataSize;
} WaveChunk;
#define OUTPUT_BLOCK_COUNT 1000
#endif

#define SOURCE_DMA1  2
#define SOURCE_DMA2  4
#define MM_WOM_DMAENABLE 0xC001
#define MM_WIM_DMAENABLE 0xC002
#define BLOCK_SIZE 0x800
#define QUEUE_LENGTH      10
#define TRANSMIT_FIFO_READY 0x80
#define RECEIVE_FIFO_READY  0x40

class IOIIS: public MappedIODevice{
public:

    virtual bool __fastcall PowerOn(void);
    ~IOIIS(void);

    virtual void __fastcall SaveState(StateFiler& filer) const;
    virtual void __fastcall RestoreState(StateFiler& filer);

    virtual unsigned __int16 __fastcall ReadHalf(unsigned __int32 IOAddress);
    virtual unsigned __int32 __fastcall ReadWord(unsigned __int32 IOAddress);
    virtual void __fastcall WriteHalf(unsigned __int32 IOAddress, unsigned __int16 Value);
    virtual void __fastcall WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value);

    void SetOutputDMA(bool on );
    void FlushOutput();
    void QueueOutput(void * address, unsigned int size);

    void SetInputDMA(bool on );
    void QueueInput(void * address, unsigned int size);

private:

    // Make sure that the buffer are properly aligned for best performance
    unsigned char OutputBuffer[BLOCK_SIZE*QUEUE_LENGTH*2];
    unsigned char InputBuffer[BLOCK_SIZE*QUEUE_LENGTH*2];

    bool InitAudio(void);
    void DisableOutput(void);
    void ResetCurrentOutputQueue();
    bool OutputQueueSwitchPossible();
    void SwitchOutputQueue();
    void PlayOutputQueue();
    void ResetCurrentInputQueue();
    bool InputQueueSwitchPossible();
    void SwitchInputQueue();
    void RecordInputQueue();
    void DisableInput(void);
    static DWORD WINAPI AudioIOLoopStatic(LPVOID lpvThreadParam);
    DWORD WINAPI AudioIOLoop(void);

    union{
        unsigned __int16 HalfWord;
        struct{
            unsigned __int16 IISInterface:1;
            unsigned __int16 IISPrescaler:1;
            unsigned __int16 RecvChIdleCmd:1;
            unsigned __int16 TransmitChIdleCmd:1;
            unsigned __int16 RecvDMAServiceReq:1;
            unsigned __int16 TrasmitDMAServiceReq:1;
            unsigned __int16 ReceiveFIFOReadyFlag:1;
            unsigned __int16 TransmitFIFOReadyFlag:1;
            unsigned __int16 LeftRightChIndex:1;
            unsigned __int16 Reserved1:7;
        }Bits;
    }IISCON;

    union{
        unsigned __int16 HalfWord;
        struct{
            unsigned __int16 SerBitClkFreqSelect:2;
            unsigned __int16 PrimaryClkFreqSelect:1;
            unsigned __int16 SerDataBitPerChannel:1;
            unsigned __int16 SerialInterfaceFormat:1;
            unsigned __int16 ActiveLvlLeftRightCh:1;
            unsigned __int16 TransmitRecvModeSelect:2;
            unsigned __int16 PrimarySecondaryModeSelect:1;
            unsigned __int16 Reserved:6;
        }Bits;
    }IISMOD;

    union{
        unsigned __int16 HalfWord;
        struct{
            unsigned __int16 PrescalerControlB:5;
            unsigned __int16 PrescalerControlA:5;
        }Bits;
    }IISPSR;

    union{
        unsigned __int16 HalfWord;
        struct{
            unsigned __int16 RecvFIFODataCount:6;
            unsigned __int16 TransmitFIFODataCoutn:6;
            unsigned __int16 ReceiveFIFO:1;
            unsigned __int16 TransmitFIFO:1;
            unsigned __int16 ReceiveFIFOAccessModeSelect:1;
            unsigned __int16 TransmitFIFOAccessModeSelect:1;
        }Bits;
    }IISFCON;

    union{
        unsigned __int16 HalfWord;
        struct{
            unsigned __int16 FENTRY:16;
        }Bits;
    }IISFIFO;

    HWAVEOUT m_outDevice;
    HWAVEIN  m_inDevice;
    WAVEHDR  m_outputHeaders[2];
    WAVEHDR * m_currOutputHeader, * m_currInputHeader;
    bool m_outputDMA, m_SwitchOutputQueue;
    WAVEHDR  m_inputHeaders[2];
    bool m_inputDMA, m_SwitchInputQueue;
    DWORD m_dwAudioThreadId;

#ifdef AUDIO_TESTING
    unsigned output_block_count;
    FILE * output_file;
#endif
};

class IOClockAndPower: public MappedIODevice{
public:
    virtual void __fastcall SaveState(StateFiler& filer) const;
    virtual void __fastcall RestoreState(StateFiler& filer);

    virtual unsigned __int16 __fastcall ReadHalf(unsigned __int32 IOAddress);
    virtual unsigned __int32 __fastcall ReadWord(unsigned __int32 IOAddress);
    virtual void __fastcall WriteHalf(unsigned __int32 IOAddress, unsigned __int16 Value);
    virtual void __fastcall WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value);

private:
    union{
        unsigned __int32 Word;
        struct{
            unsigned __int32 M_LTIME:12;
            unsigned __int32 U_LTIME:12;
            unsigned __int32 Reserved1:8;
        }Bits;
    }LOCKTIME;        //word

    typedef union{
        unsigned __int32 Word;
        struct{
            unsigned __int32 SDIV:2;
            unsigned __int32 PDIV:6;
            unsigned __int32 MDIV:8;
            unsigned __int32 Reserved1:16;
        }Bits;
    }PLLCON;    //word

    PLLCON MPLLCON;
    PLLCON UPLLCON;
    
    union{
        unsigned __int32 Word;
        struct{
            unsigned __int32 Reserved:2;
            unsigned __int32 Idle:1;
            unsigned __int32 PowerOff:1;
            unsigned __int32 NANDFlashController:1;
            unsigned __int32 LCDC:1;
            unsigned __int32 USBHost:1;
            unsigned __int32 USBDevice:1;
            unsigned __int32 PWMTimer:1;
            unsigned __int32 SDI:1;
            unsigned __int32 UART0:1;
            unsigned __int32 UART1:1;
            unsigned __int32 UART2:1;
            unsigned __int32 GPIO:1;
            unsigned __int32 RTC:1;
            unsigned __int32 ADC:1;
            unsigned __int32 IIC:1;
            unsigned __int32 IIS:1;
            unsigned __int32 SPI:1;
            unsigned __int32 Reserved2:13;
        }Bits;
    }CLKCON;            //word

    union{
        unsigned __int8 Byte;
        struct{
            unsigned __int8 SLOW_VAL:3;
            unsigned __int8 Reserved:1;
            unsigned __int8 SLOW_BIT:1;
            unsigned __int8 MPLL_OFF:1;
            unsigned __int8 Reserved1:1;
            unsigned __int8 UCLK_ON:1;
        }Bits;
    }CLKSLOW;        //byte

    union{
        unsigned __int8 Byte;
        struct{
            unsigned __int8 PDIVN:1;
            unsigned __int8 HDIVN:1;
            unsigned __int8 Reserved1:1;
            unsigned __int8 Reserved2:5;
        }Bits;
    }CLKDIVN;        //byte
};

class IOMemoryController: public MappedIODevice{
public:
    virtual void __fastcall SaveState(StateFiler& filer) const;
    virtual void __fastcall RestoreState(StateFiler& filer);

    virtual unsigned __int16 __fastcall ReadHalf(unsigned __int32 IOAddress);
    virtual unsigned __int32 __fastcall ReadWord(unsigned __int32 IOAddress);
    virtual void __fastcall WriteHalf(unsigned __int32 IOAddress, unsigned __int16 Value);
    virtual void __fastcall WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value);

private:
    union{
        unsigned __int32 Word;
        struct{
            unsigned __int32 Reserved1:1;
            unsigned __int32 DW0:2;
            unsigned __int32 Reserved2:1;
            unsigned __int32 DW1:2;
            unsigned __int32 WS1:1;
            unsigned __int32 ST1:1;
            unsigned __int32 DW2:2;
            unsigned __int32 WS2:1;
            unsigned __int32 ST2:1;
            unsigned __int32 DW3:2;
            unsigned __int32 WS3:1;
            unsigned __int32 ST3:1;
            unsigned __int32 DW4:2;
            unsigned __int32 WS4:1;
            unsigned __int32 ST4:1;
            unsigned __int32 DW5:2;
            unsigned __int32 WS5:1;
            unsigned __int32 ST5:1;
            unsigned __int32 DW6:2;
            unsigned __int32 WS6:1;
            unsigned __int32 ST6:1;
            unsigned __int32 DW7:2;
            unsigned __int32 WS7:1;
            unsigned __int32 ST7:1;
        }Bits;
    }BWSCON;

    typedef union{
        unsigned __int16 HalfWord;
        struct{
            unsigned __int16 PMC:2;
            unsigned __int16 Tacp:2;
            unsigned __int16 Tcah:2;
            unsigned __int16 Toch:2;
            unsigned __int16 Tacc:3;
            unsigned __int16 Tcos:2;
            unsigned __int16 Tacs:2;
            unsigned __int16 Reserved1:1;
        }Bits;
    }BANKCONa;
    
    BANKCONa BANKCON0;
    BANKCONa BANKCON1;
    BANKCONa BANKCON2;
    BANKCONa BANKCON3;
    BANKCONa BANKCON4;
    BANKCONa BANKCON5;

    typedef union{
        unsigned __int32 Word;
        struct{
            unsigned __int32 SCAN:2;
            unsigned __int32 Trcd:2;
            unsigned __int32 PMC:2;
            unsigned __int32 Tacp:2;
            unsigned __int32 Tcah:2;
            unsigned __int32 Toch:2;
            unsigned __int32 Tacc:3;
            unsigned __int32 Tcos:2;
            unsigned __int32 Tacs:2;
            unsigned __int32 MT:2;
            unsigned __int32 Reserved1:15;
        }Bits;
    }BANKCONb;

    BANKCONb BANKCON6;
    BANKCONb BANKCON7;

    union{
        unsigned __int32 Word;
        struct{
            unsigned __int32 RefreshCounter:11;
            unsigned __int32 Reserved1:5;
            unsigned __int32 Reserved2:2;
            unsigned __int32 Trc:2;
            unsigned __int32 Trp:2;
            unsigned __int32 TREFMD:1;
            unsigned __int32 REFEN:1;
            unsigned __int32 Reserved3:8;
        }Bits;
    }REFRESH;

    union{
        unsigned __int8 Byte;
        struct{
            unsigned __int8 BK76MAP:3;
            unsigned __int8 Reserved1:1;
            unsigned __int8 SCLK_EN:1;
            unsigned __int8 SCKE_EN:1;
            unsigned __int8 Reserved2:1;
            unsigned __int8 BURST_EN:1;
        }Bits;
    }BANKSIZE;

    typedef union{
        unsigned __int16 HalfWord;
        struct{
            unsigned __int16 BL:3;
            unsigned __int16 BT:1;
            unsigned __int16 CL:3;
            unsigned __int16 TM:2;
            unsigned __int16 WBL:1;
            unsigned __int16 Reserved1:2;
            unsigned __int16 Reserved2:4;
        }Bits;
    }MRSRB;

    MRSRB MRSRB6;
    MRSRB MRSRB7;

};

class IOWatchDogTimer: public MappedIODevice{
public:
    virtual void __fastcall WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value);
};

class IORealTimeClock: public MappedIODevice{
public:
    virtual void __fastcall SaveState(StateFiler& filer) const;
    virtual void __fastcall RestoreState(StateFiler& filer);

    virtual unsigned __int32 __fastcall ReadWord(unsigned __int32 IOAddress);
    virtual void __fastcall WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value);

private:
    union{
        unsigned __int8 Byte;
        struct{
            unsigned __int8 RTCEN:1;
            unsigned __int8 CLKSEL:1;
            unsigned __int8 CNTSEL:1;
            unsigned __int8 CLKRST:1;
            unsigned __int8 Reserved1:4;
        }Bits;
    }RTCCON;

    FILETIME m_ftRTCAdjust;
    SYSTEMTIME m_stRTC;
    bool m_fRTCWritten;
};

typedef class IOHardwareUART: public MappedIODevice{
public:
    virtual bool __fastcall PowerOn();
    virtual bool __fastcall Reconfigure(__in_z const wchar_t * NewParam);

    virtual void __fastcall SaveState(StateFiler& filer) const;
    virtual void __fastcall RestoreState(StateFiler& filer);

    virtual unsigned __int8 __fastcall ReadByte(unsigned __int32 IOAddress);
    virtual unsigned __int32 __fastcall ReadWord(unsigned __int32 IOAddress);
    virtual void __fastcall WriteByte(unsigned __int32 IOAddress, unsigned __int8 Value);
    virtual void __fastcall WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value);

    void ChangeDTR(unsigned __int32 DTRValue);
    bool GetDSR(void);

    unsigned __int16 GetLevelSubInterruptsPending(void);

protected:
    void __fastcall UpdateComPortSettings(unsigned __int32 NewULCON, unsigned __int32 NewUBRDIV);

    typedef union {
        unsigned __int8 Byte;
        struct{
            unsigned __int8 WordLength:2;
            unsigned __int8 StopBits:1;
            unsigned __int8 ParityMode:3;
            unsigned __int8 InfraRed:1;
            unsigned __int8 Reserved1:1;
        } Bits;
    }ULCONRegister;

    ULCONRegister ULCON;
    
    union {
        unsigned __int16 HalfWord;
        struct{
            unsigned __int16 ReceiveMode:2;
            unsigned __int16 TransmitMode:2;
            unsigned __int16 SendBreakSignal:1;
            unsigned __int16 LoopbackMode:1;
            unsigned __int16 RxErrorStatusInterruptEnable:1;
            unsigned __int16 RxTimeOutEnable:1;
            unsigned __int16 RxInterruptType:1;
            unsigned __int16 TxInterruptType:1;
            unsigned __int16 ClockSelection:1;
            unsigned __int16 Reserved1:5;
        }Bits;
    }UCON;

    union {
        unsigned __int8 Byte;
        struct{
            unsigned __int8 FIFOEnable:1;
            unsigned __int8 RxFIFOReset:1;
            unsigned __int8 TxFIFOReset:1;
            unsigned __int8 Reserved:1;
            unsigned __int8 RxFIFOTriggerLevel:2;
            unsigned __int8 TxFIFOTriggerLevel:2;
        } Bits;
    }UFCON;

    union{
        unsigned __int8 Byte;
        struct{
            unsigned __int8 RequestToSend:1;
            unsigned __int8 Reserved1:3;
            unsigned __int8 AFC:1;
            unsigned __int8 Reserved2:3;
        }Bits;
    }UMCON;

    union{
        unsigned __int8 Byte;
        struct{
            unsigned __int8 ReceiveBufferDataReady:1;
            unsigned __int8 TransmitBufferEmpty:1;
            unsigned __int8 TransmitEmpty:1;
            unsigned __int8 Reserved1:5;
        }Bits;
    }UTRSTAT;

    union{
        unsigned __int8 Byte;
        struct{
            unsigned __int8 OverrunError:1;
            unsigned __int8 ParityError:1;
            unsigned __int8 FrameError:1;
            unsigned __int8 BreakDetect:1;
            unsigned __int8 Reserved:4;
        }Bits;
    }UERSTAT;

    union{
        unsigned __int16 HalfWord;
        struct{
            unsigned __int16 RxFIFOCount:4;
            unsigned __int16 TxFIFOCount:4;
            unsigned __int16 RxFIFOFull:1;
            unsigned __int16 TxFIFOFull:1;
            unsigned __int16 Reserved1:6;
        }Bits;
    }UFSTAT;

    union{
        unsigned __int8 Byte;
        struct{
            unsigned __int8 CTS:1;
            unsigned __int8 DSR:1;
            unsigned __int8 DeltaCTS:1;
            unsigned __int8 DeltaDSR:1;
            unsigned __int8 Reserved3:4;
        }Bits;
    }UMSTAT;

    union{
        unsigned __int8 Byte;
        struct{
            unsigned __int8 TXDATA:8;
        }Bits;
    }UTXH;

    union{
        unsigned __int8 Byte;
        struct{
            unsigned __int8 RXDATA:8;
        }Bits;
    }URXH;

    unsigned __int16 UBRDIV;

    HANDLE hCom;

    static const DWORD CommMask = EV_BREAK|EV_CTS|EV_ERR|EV_DSR;

    unsigned int DeviceNumber;
    IOInterruptController::InterruptSubSource TxInterruptSubSource;
    IOInterruptController::InterruptSubSource RxInterruptSubSource;
    IOInterruptController::InterruptSubSource ErrInterruptSubSource;

    #define UART_QUEUE_LENGTH 8192
    unsigned __int8 RxQueue[UART_QUEUE_LENGTH];
    int RxQueueHead;
    int RxQueueTail;
    int RxFIFOCount;

    OVERLAPPED OverlappedRead;
    OVERLAPPED OverlappedWrite;
    OVERLAPPED OverlappedCommEvent;
    COMPLETIONPORT_CALLBACK Callback;
    static void CompletionRoutineStatic(LPVOID lpParameter, DWORD dwBytesTransferred, LPOVERLAPPED lpOverlapped);
    void CompletionRoutine(DWORD dwBytesTransferred, LPOVERLAPPED lpOverlapped);
    char ReceiveBuffer[UART_QUEUE_LENGTH];
    void BeginAsyncRead(void);
    void BeginAsyncWaitCommEvent(void);
    DWORD dwEvtMask;
    bool fShouldWaitForNextCommEvent;

} IOHardwareUART;

class IOUART0: public IOHardwareUART{
public:
    virtual bool __fastcall PowerOn();
};

class IOUART1: public IOHardwareUART{
public:
    virtual bool __fastcall PowerOn();
};

class IOUART2: public IOHardwareUART{
public:
    virtual bool __fastcall PowerOn();
};

class IOPWMTimer: public MappedIODevice {
public:
        IOPWMTimer();
        virtual ~IOPWMTimer();
    virtual bool __fastcall PowerOn(void);

    virtual void __fastcall SaveState(StateFiler& filer) const;
    virtual void __fastcall RestoreState(StateFiler& filer);

    virtual unsigned __int32 __fastcall ReadWord(unsigned __int32 IOAddress);
    virtual void __fastcall WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value);
    unsigned __int32 GetLevelInterruptsPending(void);

private:
    static DWORD WINAPI TimerThreadProc(LPVOID lpvThreadParam);
    __int64 ComputeDueTime(unsigned __int8 Prescale, int MUXInput, unsigned __int32 CountBuffer);
    void ActivateTimer(HANDLE hTimerX, unsigned __int32 TCNTBX, 
                       int MUXInput, LARGE_INTEGER * TimerXStartTime, LARGE_INTEGER * TimerXPeriod);
    static void ReloadTimer(bool TimerXAutoReload, HANDLE hTimerX,
                       LARGE_INTEGER * TimerXStartTime, 
                       LARGE_INTEGER * TimerXPeriod);
    unsigned __int32 CalcObservationReg(LARGE_INTEGER * TimerXStartTime, LARGE_INTEGER * TimerXPeriod,
                                        __int32 TCNTBx, bool TimerXAutoReload, bool TimerXIntPending);

    // Timer input clock frequency = PCLK/{prescaler value+1}/{divider value}
    // Where:
    //   PCLK=101500000 (101.5MHz)
    //   prescalar value = 0...255
    //   divider value   = 2, 4, 8, or 16
    union {
        unsigned __int32 Word;
        struct {
            unsigned __int32 Prescaler0:8;        // Prescaler for timers 0 and 1, set to 0n200 by WinCE
            unsigned __int32 Prescaler1:8;        // Prescaler for timers 2, 3, and 4
            unsigned __int32 DeadZoneLength:8;
            unsigned __int32 Reserved1:8;
        } Bits;
    } TCFG0;

    union {
        unsigned __int32 Word;
        struct {
            unsigned __int32 MUX0:4;
            unsigned __int32 MUX1:4;
            unsigned __int32 MUX2:4;
            unsigned __int32 MUX3:4;
            unsigned __int32 MUX4:4;  // set to 1 by WinCE, to divide by 4
            unsigned __int32 DMAMode:4;
            unsigned __int32 Reserved:8;
        } Bits;
    } TCFG1;

    typedef union {
        unsigned __int32 Word;
        struct {
            unsigned __int32 Timer0StartStop:1;
            unsigned __int32 Timer0ManualUpdate:1;
            unsigned __int32 Timer0OutputInverter:1;
            unsigned __int32 Timer0AutoReload:1;
            unsigned __int32 DeadZoneEnable:1;
            unsigned __int32 Reserved1:3;
            unsigned __int32 Timer1StartStop:1;
            unsigned __int32 Timer1ManualUpdate:1;
            unsigned __int32 Timer1OutputInverter:1;
            unsigned __int32 Timer1AutoReload:1;
            unsigned __int32 Timer2StartStop:1;
            unsigned __int32 Timer2ManualUpdate:1;
            unsigned __int32 Timer2OutputInverter:1;
            unsigned __int32 Timer2AutoReload:1;
            unsigned __int32 Timer3StartStop:1;
            unsigned __int32 Timer3ManualUpdate:1;
            unsigned __int32 Timer3OutputInverter:1;
            unsigned __int32 Timer3AutoReload:1;
            unsigned __int32 Timer4StartStop:1;
            unsigned __int32 Timer4ManualUpdate:1;
            unsigned __int32 Timer4AutoReload:1;
            unsigned __int32 Reserved2:9;
        } Bits;
    } TCONRegister;

    TCONRegister TCON;

    unsigned __int32 TCNTB0;
    unsigned __int32 TCMPB0;

    unsigned __int32 TCNTB1;
    unsigned __int32 TCMPB1;

    unsigned __int32 TCNTB2;
    unsigned __int32 TCMPB2;

    unsigned __int32 TCNTB3;
    unsigned __int32 TCMPB3;

    unsigned __int32 TCNTB4;

    HANDLE hTimerThread;
    HANDLE hTimer2;
    HANDLE hTimer3;
    HANDLE hTimer4;
    LARGE_INTEGER Timer2StartTime;
    LARGE_INTEGER Timer3StartTime;
    LARGE_INTEGER Timer4StartTime;
    LARGE_INTEGER Timer2Period;
    LARGE_INTEGER Timer3Period;
    LARGE_INTEGER Timer4Period;
    LARGE_INTEGER PerformanceCounterFrequency;
    bool Timer2InterruptPending;
    bool Timer3InterruptPending;
    bool Timer4InterruptPending;
    UINT MultimediaTimerPeriod;
};

class IOGPIO: public MappedIODevice {
public:
    virtual bool __fastcall Reset(void);

    virtual void __fastcall SaveState(StateFiler& filer) const;
    virtual void __fastcall RestoreState(StateFiler& filer);

    virtual unsigned __int16 __fastcall ReadHalf(unsigned __int32 IOAddress);
    virtual unsigned __int32 __fastcall ReadWord(unsigned __int32 IOAddress);
    virtual void __fastcall WriteHalf(unsigned __int32 IOAddress, unsigned __int16 Value);
    virtual void __fastcall WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value);
    void RaiseInterrupt(unsigned int InterruptNum);
    void ClearInterrupt(unsigned int InterruptNum);
    void PowerButtonPressed(void);
    unsigned __int32 GetLevelInterruptsPending(void);

private:

    // cf. Samsung S3C2410X User's Manual pg. 9-7
/*
    typedef union {
        unsigned __int16 HalfWord;
        struct {
            unsigned __int16 GP0:2;
            unsigned __int16 GP1:2;
            unsigned __int16 GP2:2;
            unsigned __int16 GP3:2;
            unsigned __int16 GP4:2;
            unsigned __int16 GP5:2;
            unsigned __int16 GP6:2;
            unsigned __int16 GP7:2;
        } Bits;
    } IOControlRegister7;

    typedef union {
        unsigned __int32 Word;
        struct{
            unsigned __int32 GP0:2;
            unsigned __int32 GP1:2;
            unsigned __int32 GP2:2;
            unsigned __int32 GP3:2;
            unsigned __int32 GP4:2;
            unsigned __int32 GP5:2;
            unsigned __int32 GP6:2;
            unsigned __int32 GP7:2;
            unsigned __int32 GP8:2;
            unsigned __int32 GP9:2;
            unsigned __int32 GP10:2;
            unsigned __int32 Reserved:10;
        } Bits;
    } IOControlRegister10;

    typedef union {
        unsigned __int32 Word;
        struct {
            unsigned __int32 GP0:2;
            unsigned __int32 GP1:2;
            unsigned __int32 GP2:2;
            unsigned __int32 GP3:2;
            unsigned __int32 GP4:2;
            unsigned __int32 GP5:2;
            unsigned __int32 GP6:2;
            unsigned __int32 GP7:2;
            unsigned __int32 GP8:2;
            unsigned __int32 GP9:2;
            unsigned __int32 GP10:2;
            unsigned __int32 GP11:2;
            unsigned __int32 GP12:2;
            unsigned __int32 GP13:2;
            unsigned __int32 GP14:2;
            unsigned __int32 GP15:2;
        } Bits;
    } IOControlRegister15;

    typedef union {
        unsigned __int32 Word;
        struct{
            unsigned __int32 GP0:1;
            unsigned __int32 GP1:1;
            unsigned __int32 GP2:1;
            unsigned __int32 GP3:1;
            unsigned __int32 GP4:1;
            unsigned __int32 GP5:1;
            unsigned __int32 GP6:1;
            unsigned __int32 GP7:1;
            unsigned __int32 GP8:1;
            unsigned __int32 GP9:1;
            unsigned __int32 GP10:1;
            unsigned __int32 GP11:1;
            unsigned __int32 GP12:1;
            unsigned __int32 GP13:1;
            unsigned __int32 GP14:1;
            unsigned __int32 GP15:1;
            unsigned __int32 GP16:1;
            unsigned __int32 GP17:1;
            unsigned __int32 GP18:1;
            unsigned __int32 GP19:1;
            unsigned __int32 GP20:1;
            unsigned __int32 GP21:1;
            unsigned __int32 GP22:1;
            unsigned __int32 Reserved:9;
        } Bits;
    } IOControlRegister22;

    union{
        unsigned __int32 Word;
        struct {
            unsigned __int32 SPUCR0:1;
            unsigned __int32 SPUCR1:1;
            unsigned __int32 Reserved1:1;
            unsigned __int32 USBPAD:1;
            unsigned __int32 CLKSEL0:2;
            unsigned __int32 Reserved2:1;
            unsigned __int32 CLKSEL1:2;
            unsigned __int32 Reserved3:1;
            unsigned __int32 USBSUSPND0:1;
            unsigned __int32 USBSUSPND1:1;
            unsigned __int32 Reserved4:2;
            unsigned __int32 nRSTCON:1;
            unsigned __int32 nEN_SCLK0:1;
            unsigned __int32 nEN_SCLK1:1;
            unsigned __int32 nEN_SCKE:1;
            unsigned __int32 Reserved5:2;
            unsigned __int32 Reserved6:10;
        } Bits;
    } MISCCR;                        // 0x56000080

    union{
        unsigned __int8 Byte;
        struct{
            unsigned __int8 PWRST:1;
            unsigned __int8 OFFRST:1;
            unsigned __int8 WTDRST:1;
            unsigned __int8 Reserved1:5;
        }Bits;
    }GSTATUS2;

*/
    typedef unsigned __int16 IOControlRegister7;
    typedef unsigned __int32 IOControlRegister10;
    typedef unsigned __int32 IOControlRegister15;
    typedef unsigned __int32 IOControlRegister22;


    // Port A Control Registers
    IOControlRegister22 GPACON;        // 0x56000000
    unsigned __int32 GPADAT;        // 0x56000004
                                    // 0x56000008 - reserved
                                    // 0x5600000c - reserved

    // Port B Control Registers
    IOControlRegister10 GPBCON;        // 0x56000010
    unsigned __int16 GPBDAT;        // 0x56000014
    unsigned __int16 GPBUP;            // 0x56000018
                                    // 0x5600001c - reserved

    // Port C Control Registers
    IOControlRegister15 GPCCON;        // 0x56000020
    unsigned __int16 GPCDAT;        // 0x56000024
    unsigned __int16 GPCUP;            // 0x56000028
                                    // 0x5600002c - reserved

    // Port D Control Registers
    IOControlRegister15 GPDCON;        // 0x56000030
    unsigned __int16 GPDDAT;        // 0x56000034
    unsigned __int16 GPDUP;            // 0x56000038
                                    // 0x5600003c - reserved

    // Port E Control Registers
    IOControlRegister15 GPECON;        // 0x56000040
    unsigned __int16 GPEDAT;        // 0x56000044
    unsigned __int16 GPEUP;            // 0x56000048
                                    // 0x5600004c - reserved

    // Port F Control Registers
    IOControlRegister7 GPFCON;        // 0x56000050
    unsigned __int8 GPFDAT;            // 0x56000054
    unsigned __int8 GPFUP;            // 0x56000058
                                    // 0x5600005c - reserved

    // GPG... registers
    IOControlRegister15 GPGCON;        // 0x56000060
    unsigned __int16 GPGDAT;        // 0x56000064
    unsigned __int16 GPGUP;            // 0x56000068
                                    // 0x5600006c

    // GPH.. registers
    IOControlRegister10 GPHCON;        // 0x56000070
    unsigned __int16 GPHDAT;        // 0x56000074
    unsigned __int16 GPHUP;            // 0x56000078
                                    // 0x5600007c
    unsigned __int32 MISCCR;

    unsigned __int32 DCLKCON;        // 0x56000084
    
    unsigned __int32 EINTFLT2;        // 0x5600009c
    unsigned __int32 EINTFLT3;        // 0x560000a0


    unsigned __int8 GSTATUS2;
    unsigned __int32 GSTATUS3;

    unsigned __int32 EXTINT0;
    unsigned __int32 EXTINT1;
    unsigned __int32 EXTINT2;

    unsigned __int32 EINTMASK;
    unsigned __int32 EINTPEND;

    unsigned __int32 EINTLevel;        // collection of level-triggered interrupts that are pending

    bool PowerButtonWasPressed;
    int GPFDATReadCount;
};

// The samsung BSP supports where restricted NAND configuration
#define NAND_SECTOR_SIZE 520      // Bytes per sector
#define NAND_CONFIG_ADDR 512      // Sector info structure is stored at that offset
#define NAND_BLOCK_SIZE 32        // Sectors per block
#define NAND_BLOCK_COUNT 4*1024   // Block in the chip

// This emulates the SMDK2410 NAND flash controller  See smdk2410\nand\fmd\fmd.c.
class IONANDFlashController: public MappedIODevice {
public:
    IONANDFlashController();
    ~IONANDFlashController();

    virtual bool __fastcall PowerOn();
    virtual void __fastcall SaveState(StateFiler& filer) const;
    virtual void __fastcall RestoreState(StateFiler& filer);
    virtual bool __fastcall SaveStateToFile(StateFiler& filer, __in_z const wchar_t * flashFileName) const;
    virtual bool __fastcall RestoreStateFromFile(StateFiler& filer, __in_z const wchar_t * flashFileName);
    virtual bool __fastcall IONANDFlashController::InitializeFlashFromResource();

    virtual void __fastcall WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value);
    virtual void __fastcall WriteByte(unsigned __int32 IOAddress, unsigned __int8 Value);
    virtual unsigned __int32 __fastcall ReadWord(unsigned __int32 IOAddress);
    virtual unsigned __int8 __fastcall ReadByte(unsigned __int32 IOAddress);

    void setSavedFileName(__in_z const wchar_t * filename);
    inline LONG incrementSavedStateLock() {return InterlockedIncrement((LONG*)&m_SaveCount);}
    inline LONG decrementSavedStateLock() {return InterlockedDecrement((LONG*)&m_SaveCount);}

    static DWORD WINAPI NANDFlashSaveStatic(LPVOID lpvThreadParam);

private:
    unsigned __int32 NFCONF;
    unsigned __int32 NFADDR;
    unsigned __int32 NFSTAT;
    unsigned __int8  NFCMD;
    unsigned __int32 m_NumberBlocks;
    unsigned __int32 m_SectorsPerBlock;
    unsigned __int32 m_BytesPerSector;

    unsigned __int8 * m_Flash;
    wchar_t * m_SavedFileName;
    unsigned int m_SaveCount;

    unsigned int BytesRead;
};

// This emulates the AMD AM29LV800BB flash controller used by eboot's am29lv800.c
class IOAM29LV800BB: public MappedIODevice {
public:
    IOAM29LV800BB();
    virtual void __fastcall SaveState(StateFiler& filer) const;
    virtual void __fastcall RestoreState(StateFiler& filer);

    virtual void __fastcall WriteHalf(unsigned __int32 IOAddress, unsigned __int16 Value);

    void InitializeEbootConfig(unsigned __int16* MacAddress);

private:
    size_t __fastcall SectorSizeFromAddress(unsigned __int32 IOAddress);

    unsigned __int16 CMD;
    unsigned __int32 CachedDataValue; // We cache data contents when we override them 
};


// This typedef declares the base class, IOSPI, for both SPI0 and SPI1
typedef class IOSPI: public MappedIODevice {
public:
    IOSPI();

    virtual void __fastcall SaveState(StateFiler& filer) const;
    virtual void __fastcall RestoreState(StateFiler& filer);

protected:
    union {
        unsigned __int32 Word;
        struct {
            unsigned __int32 TAGD:1; // Tx Auto Garbage Data mode enable
            unsigned __int32 CPHA:1; // Clock Phase Select
            unsigned __int32 CPOL:1; // Clock Polarity Select
            unsigned __int32 MSTR:1; // Primary/Secondary Select
            unsigned __int32 ENSCK:1; // SCK Enable
            unsigned __int32 SMOD:2; // SPI Mode Select (00=polling, 01=interrupt, 10=DMA, 11=reserved)
            unsigned __int32 Reserved1:25;
        } Bits;
    } SPCON;

    union {
        unsigned __int32 Word;
        struct {
            unsigned __int32 REDY:1;    // Transfer Ready Flag
            unsigned __int32 MULF:1;    // Multi Primary Error Flag
            unsigned __int32 DCOL:1;    // Data Collision Error Flag
            unsigned __int32 Reserved1:29;
        } Bits;
    } SPSTA;

    union {
        unsigned __int32 Word;
        struct {
            unsigned __int32 KEEP:1;    // Primary Out Keep
            unsigned __int32 Reserved1:1;
            unsigned __int32 ENMUL:1;    // Multi Primary error detect Enable
            unsigned __int32 Reserved2:29;
        } Bits;
    } SPPIN;

    unsigned __int32 SPPRE;

    unsigned __int8 SPTDAT;
    unsigned __int8 SPRDAT;
} IOSPI;

class IOSPI1: public IOSPI {
public:
    virtual void __fastcall SaveState(StateFiler& filer) const;
    virtual void __fastcall RestoreState(StateFiler& filer);

    virtual void __fastcall WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value);
    virtual unsigned __int32 __fastcall ReadWord(unsigned __int32 IOAddress);
    void __fastcall EnqueueKey(unsigned __int8 Key);
    inline bool isQueueEmpty() { return QueueHead == QueueTail; }
private:
    #define KEYBOARD_QUEUE_LENGTH 16
    unsigned __int8 KeyboardQueue[KEYBOARD_QUEUE_LENGTH];
    unsigned int QueueHead;
    unsigned int QueueTail;
};


class IOLCDController: public IOWinController {
public:
    IOLCDController();

    virtual bool __fastcall PowerOn();
    virtual bool __fastcall Reset();

    virtual void __fastcall SaveState(StateFiler& filer) const;
    virtual void __fastcall RestoreState(StateFiler& filer);

    virtual void __fastcall WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value);
    virtual unsigned __int32 __fastcall ReadWord(unsigned __int32 IOAddress);

    __inline unsigned __int32 ScreenX(void) {
        return LCDCON3.Bits.HOZVAL+1;
    }

    __inline unsigned __int32 ScreenY(void) {
        return LCDCON2.Bits.LINEVAL+1;
    }

        __inline unsigned __int32 BitsPerPixel(void) {
            switch (LCDCON1.Bits.BPPMODE) {
            case 12:
                return 16;
            case 13:
                return 24;
            case 14:
                return 32;
            default:
                // We can get here while we are suspending because the BSP will zero out the LCD registers
                ASSERT(LCDCON1.Word == 0);
                LCDCON1.Bits.BPPMODE=12; // force BPP back to 16
                return 16;
            }
        }

    virtual DWORD WINAPI LCDDMAThreadProc(void);

private:
    bool   LCDEnabled();
    size_t getFrameBuffer(); 

    // Event callbacks
    void onWM_LBUTTONDOWN( LPARAM lParam );
    void onWM_MOUSEMOVE( LPARAM lParam );
    void onWM_LBUTTONUP( LPARAM lParam );
    void onWM_CAPTURECHANGED();
    void onWM_SYSKEYUP(unsigned __int8 & KeyUpDown) { KeyUpDown=0x80; }
    void onWMKEY( unsigned __int8 vKey, unsigned __int8 & KeyUpDown );

    // Device specific menu option callback
    static bool onID_FLASH_SAVE(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    union {
        unsigned __int32 Word;
        struct {
            unsigned __int32 ENVID:1;
            unsigned __int32 BPPMODE:4;
            unsigned __int32 PNRMODE:2;
            unsigned __int32 MMODE:1;
            unsigned __int32 CLKVAL:10;
            unsigned __int32 LINECNT:10;
            unsigned __int32 Reserved1:4;
        } Bits;
    } LCDCON1;

    union {
        unsigned __int32 Word;
        struct {
            unsigned __int32 VSPW:6;
            unsigned __int32 VFPD:8;
            unsigned __int32 LINEVAL:10;    // Y size of the display-1 (ie. 319 or 239)
            unsigned __int32 VPBD:8;
        } Bits;
    } LCDCON2;

    union {
        unsigned __int32 Word;
        struct {
            unsigned __int32 LINEBLANK_HFPD:8;
            unsigned __int32 HOZVAL:11;        // X size of the display-1 (ie. 239 or 319)
            unsigned __int32 WDLY_HBPD:7;
            unsigned __int32 Reserved1:5;
        } Bits;
    } LCDCON3;

    union {
        unsigned __int32 Word;
        struct {
            unsigned __int32 WLH_HSPW:8;
            unsigned __int32 MVAL:8;
            unsigned __int32 Reserved1:16;
        } Bits;
    } LCDCON4;

    union {
        unsigned __int32 Word;
        struct {
            unsigned __int32 HWSWP:1;
            unsigned __int32 BSWP:1;
            unsigned __int32 ENLEND:1;
            unsigned __int32 PWREN:1;
            unsigned __int32 INVLEND:1;
            unsigned __int32 INVPWREN:1;
            unsigned __int32 INVVDEN:1;
            unsigned __int32 INVVD:1;
            unsigned __int32 INVVFRAME:1;
            unsigned __int32 INVVLINE:1;
            unsigned __int32 INVVCLOCK:1;
            unsigned __int32 FRM565:1;
            unsigned __int32 BPP24BL:1;
            unsigned __int32 Reserved1:4;
            unsigned __int32 HSTATUS:2;
            unsigned __int32 VSTATUS:2;
            unsigned __int32 Reserved2:9;
        } Bits;
    } LCDCON5;

    union {
        unsigned __int32 Word;
        struct {
            unsigned __int32 LCDBASEU:21;    // Upper 21 bits of FRAMEBUF_DMA_BASE
            unsigned __int32 LCDBANK:9;     // Bits 1-20 of FRAMEBUF_DMA_BASE
            unsigned __int32 Reserved1:2;
        } Bits;
    } LCDSADDR1;

    union {
        unsigned __int32 Word;
        struct {
            unsigned __int32 LCDBASEL:21;    // End address of the LCD frame buffer, but without its low bit
            unsigned __int32 Reserved1:11;
        } Bits;
    } LCDSADDR2;

    union {
        unsigned __int32 Word;
        struct {
            unsigned __int32 PAGEWIDTH:11;
            unsigned __int32 OFFSIZE:11;
            unsigned __int32 Reserved1:10;
        } Bits;
    } LCDSADDR3;

    unsigned __int32 REDVAL;
    unsigned __int32 GREENVAL;
    unsigned __int32 BLUEVAL;
    unsigned __int32 DITHMODE;

    union {
        unsigned __int32 Word;
        struct {
            unsigned __int32 TPALVAL:24;
            unsigned __int32 TPALEN:1;
            unsigned __int32 Reserved1:7;
        } Bits;
    } TPAL;

    union {
        unsigned __int32 Word;
        struct {
            unsigned __int32 INT_FiCnt:1;
            unsigned __int32 INT_FrSyn:1;
            unsigned __int32 Reserved1:30;
        } Bits;
    } LCDINTPND, LCDSRCPND;


    union {
        unsigned __int32 Word;
        struct {
            unsigned __int32 INT_FiCnt:1;
            unsigned __int32 INT_FrSyn:1;
            unsigned __int32 FIWSEL:1;
            unsigned __int32 Reserved1:29;
        } Bits;
    } LCDINTMSK;

    union {
        unsigned __int32 Word;
        struct {
            unsigned __int32 LPC_EN:1;
            unsigned __int32 RES_SEL:1;
            unsigned __int32 MODE_SEL:1;
            unsigned __int32 CPV_SEL:1;
            unsigned __int32 Reserved1:28;
        } Bits;
    } LPCSEL;
    bool m_NumLockState;
};

class IODMAController : public MappedIODevice {
public:
    virtual void __fastcall SaveState(StateFiler& filer) const;
    virtual void __fastcall RestoreState(StateFiler& filer);

      virtual void __fastcall WriteWord(unsigned __int32 IOAddress, unsigned _int32 Value);
      virtual unsigned __int32 __fastcall ReadWord(unsigned __int32 IOAddress);

protected:
      union {
              unsigned __int32 Word;
              struct {
                      unsigned __int32 S_ADDR:31;
                      unsigned __int32 Reserved1:1;
              } Bits;
      } DISRC;

      union {
              unsigned __int32 Word;
              struct {
                      unsigned __int32 INC:1;
                      unsigned __int32 LOC:1;
                      unsigned __int32 Reserved1:30;
              } Bits;
      } DISRCC;

      union {
              unsigned __int32 Word;
              struct {
                      unsigned __int32 D_ADDR:31;
                      unsigned __int32 Reserved1:1;
              } Bits;
      } DIDST;

      union {
              unsigned __int32 Word;
              struct {
                      unsigned __int32 INC:1;
                      unsigned __int32 LOC:1;
                      unsigned __int32 Reserved1:30;
              } Bits;
      } DIDSTC;

      union {
              unsigned __int32 Word;
              struct {
                      unsigned __int32 TC:20;
                      unsigned __int32 DSZ:2;
                      unsigned __int32 RELOAD:1;
                      unsigned __int32 SWHW_SEL:1;
                      unsigned __int32 HWSRCSEL:3;
                      unsigned __int32 SERVMODE:1;
                      unsigned __int32 Reserved1:5;
              } Bits;
      } DCON;

      union {
              unsigned __int32 Word;
              struct {
                      unsigned __int32 CURR_TC:20;
                      unsigned __int32 STAT:2;
                      unsigned __int32 Reserved:10;
              } Bits;
      } DSTAT;

      union {
              unsigned __int32 Word;
              struct {
                      unsigned __int32 CURR_SRC:31;
                      unsigned __int32 Reserved1:1;
              } Bits;
      } DCSRC;

      union {
              unsigned __int32 Word;
              struct {
                      unsigned __int32 CURR_DST:31;
                      unsigned __int32 Reserved1:1;
              } Bits;
      } DCDST;

      union {
              unsigned __int32 Word;
              struct {
                      unsigned __int32 SW_TRIG:1;
                      unsigned __int32 ON_OFF:1;
                      unsigned __int32 STOP:1;
                      unsigned __int32 Reserved1:29;
              } Bits;
      } DMASKTRIG;
};

class IODMAController1: public IODMAController {
public:
    void __fastcall WriteWord(unsigned __int32 IOAddress, unsigned _int32 Value);
};

class IODMAController2: public IODMAController {
public:
    void __fastcall WriteWord(unsigned __int32 IOAddress, unsigned _int32 Value);
};

class IOADConverter: public MappedIODevice {
public:
    IOADConverter();

    virtual bool __fastcall Reset(void);
    virtual void __fastcall SaveState(StateFiler& filer) const;
    virtual void __fastcall RestoreState(StateFiler& filer);

    virtual void __fastcall WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value);
    virtual unsigned __int32 __fastcall ReadWord(unsigned __int32 IOAddress);

    typedef enum PenState {
        PenStateDown=0,
        PenStateUp=1
    } PenState;

    PenState GetPenState() { return (ADCDAT0.Bits.UPDOWN == 0 ? PenStateDown : PenStateUp ); }
    void SetPenState(PenState State);
    void SetPenSample(WORD x, WORD y);
    void RaiseTCInterrupt(void);    // Used to indicate that pen has changed state

private:
    union {
        unsigned __int32 Word;
        struct {
            unsigned __int32 ENABLE_START:1;
            unsigned __int32 READ_START:1;
            unsigned __int32 STDBM:1;
            unsigned __int32 SEL_MUX:3;
            unsigned __int32 PRSCVL:8;
            unsigned __int32 PRSCEN:1;
            unsigned __int32 ECFLG:1;
            unsigned __int32 Reserved1:16;
        } Bits;
    } ADCCON;

    union {
        unsigned __int32 Word;
        struct {
            unsigned __int32 XY_PST:2;
            unsigned __int32 AUTO_PST:1;
            unsigned __int32 PULL_UP:1;
            unsigned __int32 XP_SEN:1;
            unsigned __int32 XM_SEN:1;
            unsigned __int32 YP_SEN:1;
            unsigned __int32 YM_SEN:1;
            unsigned __int32 Reserved1:25;
        } Bits;
    } ADCTSC;

    union {
        unsigned __int32 Word;
        struct {
            unsigned __int16 DELAY;
            unsigned __int16 Reserved1;
        } Bits;
    } ADCDLY;

    union {
        unsigned __int32 Word;
        struct {
            unsigned __int32 XPDATA:10;
            unsigned __int32 Reserved1:2;
            unsigned __int32 XY_PST:2;
            unsigned __int32 AUTO_PST:1;
            unsigned __int32 UPDOWN:1;
            unsigned __int32 Reserved2:16;
        } Bits;
    } ADCDAT0;

    union {
        unsigned __int32 Word;
        struct {
            unsigned __int32 YPDATA:10;
            unsigned __int32 Reserved1:2;
            unsigned __int32 XY_PST:2;
            unsigned __int32 AUTO_PST:1;
            unsigned __int32 UPDOWN:1;
            unsigned __int32 Reserved2:16;
        } Bits;
    } ADCDAT1;

    unsigned __int16 SampleX;
    unsigned __int16 SampleY;
};

class IOCS8900Memory: public MappedIODevice
{
public:
    virtual void __fastcall WriteHalf(unsigned __int32 IOAddress, unsigned __int16 Value);
    virtual unsigned __int16 __fastcall ReadHalf(unsigned __int32 IOAddress);
};

class IOCS8900IO: public MappedIODevice
{
    friend class IOCS8900Memory;

public:
    virtual bool __fastcall PowerOn(void);

    virtual void __fastcall SaveState(StateFiler& filer) const;
    virtual void __fastcall RestoreState(StateFiler& filer);

    virtual void __fastcall WriteHalf(unsigned __int32 IOAddress, unsigned __int16 Value);
    virtual unsigned __int16 __fastcall ReadHalf(unsigned __int32 IOAddress);
    void ReceivePacket(void);

private:
    void __fastcall TransmitPacket(void);
    inline bool __fastcall ShouldIndicatePacketToGuest(const EthernetHeader & inEthernetHeader);
    static void __fastcall ReceiveCompletionRoutineStatic(LPVOID lpParameter, DWORD dwBytesTransferred, LPOVERLAPPED lpOverlapped);
    void __fastcall ReceiveCompletionRoutine(DWORD dwBytesTransferred);
    static void __fastcall TransmitCompletionRoutineStatic(LPVOID lpParameter, DWORD dwBytesTransferred, LPOVERLAPPED lpOverlapped);
    void BeginAsyncReceive(void);

    unsigned __int16 IO_RX_TX_DATA_1;
    unsigned __int16 IO_TX_CMD;
    unsigned __int16 IO_TX_LENGTH;
    unsigned __int16 IO_ISQ;
    unsigned __int16 IO_PACKET_PAGE_POINTER;
    unsigned __int16 IO_PACKET_PAGE_DATA_0;
    unsigned __int16 IO_PACKET_PAGE_DATA_1;
    unsigned __int16 PKTPG_LOGICAL_ADDR_FILTER[4];

    union {
        unsigned __int32 Word;
        struct {
            unsigned __int16 LowHalf;
            unsigned __int16 HighHalf;
        } HalfWords;
    } MEMORY_BASE_ADDR; // the driver stores dwEthernetMemBase here

    union {
        unsigned __int16 HalfWord;
        struct {
            unsigned __int16 Reserved1:6; // must be 010011
            unsigned __int16 SerRxON:1;
            unsigned __int16 SerTxON:1;
            unsigned __int16 AUIOnly:1;
            unsigned __int16 AutoAUI:1;
            unsigned __int16 Reserved2:1;
            unsigned __int16 ModBackOffE:1;
            unsigned __int16 PolarityDis:1;
            unsigned __int16 TwoPtDefDis:1;
            unsigned __int16 LoRxSquelch:1;
            unsigned __int16 Reserved3:1;
        } Bits;
    } LINE_CTL;

    union {
        unsigned __int16 HalfWord;
        struct {
            unsigned __int16 Reserved:6; // must be 000101
            unsigned __int16 IAHashA:1;
            unsigned __int16 PromiscuousA:1;
            unsigned __int16 RxOKA:1;
            unsigned __int16 MulticastA:1;
            unsigned __int16 IndividialA:1;
            unsigned __int16 BroadcastA:1;
            unsigned __int16 CRCerrorA:1;
            unsigned __int16 RuntA:1;
            unsigned __int16 ExtraDataA:1;
            unsigned __int16 Reserved2:1;
        } Bits;
    } RX_CTL;

    union {
        unsigned __int16 HalfWord;
        struct {
            unsigned __int16 Reserved1:10;
            unsigned __int16 MemoryE:1; // 0x400
            unsigned __int16 Reserved2:1;
            unsigned __int16 IOChrDye:1;
            unsigned __int16 Reserved3:2;
            unsigned __int16 EnableIRQ:1;
        } Bits;
    } BUS_CTL;

    unsigned __int16 BUS_ST;

    unsigned __int32 cbTxBuffer;
    unsigned __int8 TxBuffer[16384];

    unsigned int cbRxBuffer;
    unsigned __int16 *pCurrentRx;
    struct {
        unsigned __int16 RxStatus;
        unsigned __int16 FrameLength;
        unsigned __int8 Buffer[16384];
    } RxBuffer;

    class VPCNetDriver VPCNet;
    COMPLETIONPORT_CALLBACK TransmitCallback;
    COMPLETIONPORT_CALLBACK ReceiveCallback;

    HANDLE hCS8900PacketArrived;
    HANDLE hCS8900PacketSent;

public:
    USHORT MacAddress[3];    // the emulated NIC's MAC address - read/write by WinCE
};

class IOMAPPEDIOPCMCIA : public MappedIODevice {

public:

    virtual bool __fastcall PowerOn(void);
    virtual unsigned __int8 __fastcall ReadByte(unsigned __int32 IOAddress);
    virtual unsigned __int16 __fastcall ReadHalf(unsigned __int32 IOAddress);
    virtual void __fastcall WriteByte(unsigned __int32 IOAddress, unsigned __int8 Value);
    virtual void __fastcall WriteHalf(unsigned __int32 IOAddress, unsigned __int16 Value);
    
    PCMCIADevice* pDevice;
};

class IOMAPPEDMEMPCMCIA : public MappedIODevice {

public:

    virtual bool __fastcall PowerOn(void);
    virtual unsigned __int8 __fastcall ReadByte(unsigned __int32 IOAddress);
    virtual unsigned __int16 __fastcall ReadHalf(unsigned __int32 IOAddress);
    virtual void __fastcall WriteByte(unsigned __int32 IOAddress, unsigned __int8 Value);
    virtual void __fastcall WriteHalf(unsigned __int32 IOAddress, unsigned __int16 Value);
    
    PCMCIADevice* pDevice;
};

class IOCONTROLPCMCIA : public MappedIODevice {

friend class IOMAPPEDIOPCMCIA;
friend class IOMAPPEDMEMPCMCIA;

public:
    virtual bool __fastcall PowerOn(void);
    virtual bool __fastcall Reset(void);
    virtual bool __fastcall Reconfigure(__in_z const wchar_t * NewParam);
    virtual unsigned __int8 __fastcall ReadByte(unsigned __int32 IOAddress);
    virtual unsigned __int16 __fastcall ReadHalf(unsigned __int32 IOAddress);
    virtual void __fastcall WriteByte(unsigned __int32 IOAddress, unsigned __int8 Value);
    virtual void __fastcall WriteHalf(unsigned __int32 IOAddress, unsigned __int16 Value);

    void InsertCard(PCMCIADevice* pCard);
    bool IsCardInserted(void);
    void RemoveCard(void);

    void RaiseCardIRQ(void); // called by PCMCIA cards to raise SYSINTR_PCMCIA_LEVEL (EINT8)
    void ClearCardIRQ(void); // called to clear the interrupt - it is configured as level-triggered

private:
    bool MapIOAddress(unsigned __int32* pIOAddress);
    unsigned __int8 INDEX;

    unsigned __int8 REG_CHIP_INFO;
    union {
        struct {
            unsigned __int8 STS_BVD1:1;
            unsigned __int8 STS_BVD2:1;
            unsigned __int8 STS_CD1:1;
            unsigned __int8 STS_CD2:1;
            unsigned __int8 STS_WRITE_PROTECT:1;
            unsigned __int8 STS_CARD_READY:1;
            unsigned __int8 STS_CARD_POWER_ON:1;
            unsigned __int8 STS_GPI:1;
        } Bits;
        unsigned __int8 Byte;

    } REG_INTERFACE_STATUS;
    typedef union {
        struct {
            unsigned __int8 LO;
            unsigned __int8 HI;
        } Bytes;
        unsigned __int16 HalfWord;
    } MapAddress;

    MapAddress REG_IO_MAP0_START_ADDR;
    MapAddress REG_IO_MAP0_END_ADDR;
    MapAddress REG_IO_MAP1_START_ADDR;
    MapAddress REG_IO_MAP1_END_ADDR;

    unsigned __int8 REG_POWER_CONTROL;
    unsigned __int8 REG_CARD_STATUS_CHANGE;
    union {
        struct {
            unsigned __int8 CardIRQSelect:4;
            unsigned __int8 INT_ENABLE_MANAGE_INT:1;
            unsigned __int8 INT_CARD_IS_IO:1;
            unsigned __int8 INT_CARD_NOT_RESET:1;
            unsigned __int8 INT_RING_INDICATE_ENABLE:1;
        } Bits;
        unsigned __int8 Byte;
    } REG_INTERRUPT_AND_GENERAL_CONTROL;
    union {
        struct {
            unsigned __int8 CFG_BATTERY_DEAD_ENABLE:1;
            unsigned __int8 CFG_BATTERY_WARNING_ENABLE:1;
            unsigned __int8 CFG_READY_ENABLE:1;
            unsigned __int8 CFG_CARD_DETECT_ENABLE:1;
            unsigned __int8 ManagementIRQSelect:4;
        } Bits;
        unsigned __int8 Byte;
    } REG_STATUS_CHANGE_INT_CONFIG;
    union {
        struct {
            unsigned __int8 WIN_MEM_MAP0_ENABLE:1;
            unsigned __int8 WIN_MEM_MAP1_ENABLE:1;
            unsigned __int8 WIN_MEM_MAP2_ENABLE:1;
            unsigned __int8 WIN_MEM_MAP3_ENABLE:1;
            unsigned __int8 WIN_MEM_MAP4_ENABLE:1;
            unsigned __int8 WIN_MEMCS16_DECODE:1;
            unsigned __int8 WIN_IO_MAP0_ENABLE:1;
            unsigned __int8 WIN_IO_MAP1_ENABLE:1;
        } Bits;
        unsigned __int8 Byte;
    } REG_WINDOW_ENABLE;
    unsigned __int8 REG_IO_WINDOW_CONTROL;

    PCMCIADevice* pDevice;
};

#ifdef DEBUG // Currently this code doesn't work

class IOUSBHostController : public MappedIODevice {
public:
    virtual bool __fastcall PowerOn(void);
    virtual unsigned __int32 __fastcall ReadWord(unsigned __int32 IOAddress);
    virtual void __fastcall WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value);

private:
    // Control and Status Group
    //unsigned __int32 HcRevision;  // This is read-only
    unsigned __int32 HcControl;
    union {
        struct {
            unsigned __int32 HCR:1;  // Host Controller Reset
            unsigned __int32 CLR:1;
            unsigned __int32 BLF:1;
            unsigned __int32 OCR:1;
            unsigned __int32 Reserved1:12;
            unsigned __int32 SOC:2;
            unsigned __int32 Reserved2:14;
        } Bits;
        unsigned __int32 Word;
    } HcCommandStatus;
    unsigned __int32 HcInterruptStatus;
    unsigned __int32 HcInterruptEnable;
    unsigned __int32 HcInterruptDisable;

    // Memory Pointer Group
    unsigned __int32 HcHCCA;
    unsigned __int32 HcPeriodCuttentED;
    unsigned __int32 HcControlHeadED;
    unsigned __int32 HcControlCurrentED;
    unsigned __int32 HcBulkHeadED;
    unsigned __int32 HcBulkCurrentED;
    unsigned __int32 HcDoneHead;

    // Frame Counter Group
    union {
        struct {
            unsigned __int32 FrameInterval:14;
            unsigned __int32 Reserved1:2;
            unsigned __int32 FSMPS:15;
            unsigned __int32 FIT:1;
        } Bits;
        unsigned __int32 Word;
    } HcRmInterval;
    unsigned __int32 HcFmRemaining;
    unsigned __int32 HcFmNumber;
    unsigned __int32 HcPeriodicStart;
    unsigned __int32 HcLSThreshold;

    // Root Hub Group
    unsigned __int32 HcRhDescriptorA;
    unsigned __int32 HcRhDescriptorB;
    unsigned __int32 HcRhStatus;
    unsigned __int32 HcRhPortStatus1;
    unsigned __int32 HcRhPortStatus2;
};

class IOUSBDevice : public MappedIODevice {
public:
    virtual unsigned __int8 __fastcall ReadByte(unsigned __int32 IOAddress);
    virtual void __fastcall WriteByte(unsigned __int32 IOAddress, unsigned __int8 Value);

private:
    union {
        struct {
            unsigned __int8 func_addr:7;
            unsigned __int8 addr_up:1;
        } Bits;
        unsigned __int8 Byte;
    } udcFAR; // Function Address Register

    union {
        struct {
            unsigned __int8 sus_en:1;
            unsigned __int8 sus_mo:1;
            unsigned __int8 muc_res:1;
            unsigned __int8 usb_re:1;
            unsigned __int8 Reserved1:3;
            unsigned __int8 iso_up:1;
        } Bits;
        unsigned __int8 Byte;
    } PMR; // Power Management Register

    union {
        struct {
            unsigned __int8 ep0_int:1;
            unsigned __int8 ep1_int:1;
            unsigned __int8 ep2_int:1;
            unsigned __int8 ep3_int:1;
            unsigned __int8 ep4_int:1;
            unsigned __int8 Reserved1:3;
        } Bits;
        unsigned __int8 Byte;
    } EIR; // EP Interrupt  Register

    union {
        struct {
            unsigned __int8 sus_int:1;
            unsigned __int8 resume_int:1;
            unsigned __int8 reset_int:1;
            unsigned __int8 Reserved1:5;
        } Bits;
        unsigned __int8 Byte;
    } UIR; // USB Interrupt Register

    union {
        struct {
            unsigned __int8 ep0_int_en:1;
            unsigned __int8 ep1_int_en:1;
            unsigned __int8 ep2_int_en:1;
            unsigned __int8 ep3_int_en:1;
            unsigned __int8 ep4_int_en:1;
            unsigned __int8 Reserved1:3;
        } Bits;
        unsigned __int8 Byte;
    } EIER; // Interrupt Mask Register

    union {
        struct {
            unsigned __int8 sus_int_en:1;
            unsigned __int8 Reserved1:1;
            unsigned __int8 reset_int_en:1;
            unsigned __int8 Reserved2:5;
        } Bits;
        unsigned __int8 Byte;
    } UIER;

    unsigned __int8 FNR1; // Frame Number 1 Register
    unsigned __int8 FNR2; // Frame Number 2 Register
    unsigned __int8 INDEX; // Index register

    union {
        struct {
            unsigned __int8 opr_ipr:1;
            unsigned __int8 ipr:1;
            unsigned __int8 sts_ur:1;
            unsigned __int8 de_ff:1;
            unsigned __int8 se_sds:1;
            unsigned __int8 sds_sts:1;
            unsigned __int8 sopr_cdt:1;
            unsigned __int8 sse:1;
        } Bits;
        unsigned __int8 Byte;
    } EP0ICSR1; // EP0 and in CSR1 shared

    union {
        struct {
            unsigned __int8 Reserved1:4;
            unsigned __int8 in_dma_int_en:1;
            unsigned __int8 mode_in:1;
            unsigned __int8 iso:1;
            unsigned __int8 auto_set:1;
        } Bits;
        unsigned __int8 Byte;
    } ICSR2; // in CSR2

    union {
        struct {
            unsigned __int8 out_pkt_rdy:1;
            unsigned __int8 Reserved1:1;
            unsigned __int8 ov_run:1;
            unsigned __int8 data_error:1;
            unsigned __int8 fifo_flush:1;
            unsigned __int8 send_stall:1;
            unsigned __int8 sent_stall:1;
            unsigned __int8 clr_data_tog:1;
        } Bits;
        unsigned __int8 Byte;
    } OCSR1; // out CSR1

    union {
        struct {
            unsigned __int8 Reserved1:5;
            unsigned __int8 out_dma_int_en:1;
            unsigned __int8 iso:1;
            unsigned __int8 auto_clr:1;
        } Bits;
        unsigned __int8 Byte;
    } OCSR2; // out CSR2

    unsigned __int8 EP0F; // EP0 FIFO
    unsigned __int8 EP1F; // EP1 FIFO
    unsigned __int8 EP2F; // EP2 FIFO
    unsigned __int8 EP3F; // EP3 FIFO
    unsigned __int8 EP4F; // EP4 FIFO

    union {
        struct {
            unsigned __int8 maxp:4;
            unsigned __int8 Reserved1:4;
        } Bits;
        unsigned __int8 Byte;
    } MAXP; // Max Packet Register

    unsigned __int8 OFCR1; // out FIFO 1 Count1 Register
    unsigned __int8 OFCR2; // out FIFO 2 Count1 Register

    typedef union {
        struct {
            unsigned __int8 dma_mo_en:1;
            unsigned __int8 in_dma_run:1;
            unsigned __int8 orb_odr:1;
            unsigned __int8 demand_mo:1;
            unsigned __int8 state:3;
            unsigned __int8 in_run_ob:1;
        } Bits;
        unsigned __int8 Byte;
    } EP_DMA_CONTROL;

    EP_DMA_CONTROL EP1DC; // EP1 DMA Interface control
    EP_DMA_CONTROL EP2DC; // EP2 DMA Interface control
    EP_DMA_CONTROL EP3DC; // EP3 DMA Interface control
    EP_DMA_CONTROL EP4DC; // EP4 DMA Interface control

    unsigned __int8 EP1DU; // EP0 Unit Count
    unsigned __int8 EP2DU; // EP1 Unit Count
    unsigned __int8 EP3DU; // EP2 Unit Count
    unsigned __int8 EP4DU; // EP3 Unit Count
    unsigned __int8 EP1DF; // EP0 FIFO Count
    unsigned __int8 EP2DF; // EP1 FIFO Count
    unsigned __int8 EP3DF; // EP2 FIFO Count
    unsigned __int8 EP4DF; // EP3 FIFO Count
    unsigned __int8 EP1DTL; // EP0 TTL low-byte
    unsigned __int8 EP2DTL; // EP1 TTL low-byte
    unsigned __int8 EP3DTL; // EP2 TTL low-byte
    unsigned __int8 EP4DTL; // EP3 TTL low-byte
    unsigned __int8 EP1DTM; // EP0 TTL middle-byte
    unsigned __int8 EP2DTM; // EP1 TTL middle-byte
    unsigned __int8 EP3DTM; // EP2 TTL middle-byte
    unsigned __int8 EP4DTM; // EP3 TTL middle-byte
    unsigned __int8 EP1DTH; // EP0 TTL high-byte
    unsigned __int8 EP2DTH; // EP1 TTL high-byte
    unsigned __int8 EP3DTH; // EP2 TTL high-byte
    unsigned __int8 EP4DTH; // EP3 TTL high-byte


};
#endif // DEBUG - Currently this code doesn't work

#define MAPPEDIODEVICE(baseclass, devicename, iobase, iolength) \
    extern class baseclass devicename; \
    const unsigned __int32 devicename##_IOBase=iobase; \
    const unsigned __int32 devicename##_IOEnd=iolength+iobase;

#include "mappediodevices.h"
#undef MAPPEDIODEVICE
