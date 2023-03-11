/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

class IONE2000 : public PCMCIADevice {
public:
    virtual bool __fastcall PowerOn(void);
    virtual void PowerOff(void);

    virtual unsigned __int8 __fastcall ReadByte(unsigned __int32 IOAddress);
    virtual unsigned __int16 __fastcall ReadHalf(unsigned __int32 IOAddress);
    virtual void __fastcall WriteByte(unsigned __int32 IOAddress, unsigned __int8 Value);
    virtual void __fastcall WriteHalf(unsigned __int32 IOAddress, unsigned __int16 Value);

    virtual unsigned __int8  __fastcall ReadMemoryByte(unsigned __int32 IOAddress);
    virtual unsigned __int16 __fastcall ReadMemoryHalf(unsigned __int32 IOAddress);
    virtual void __fastcall WriteMemoryByte(unsigned __int32 IOAddress, unsigned __int8  Value);
    virtual void __fastcall WriteMemoryHalf(unsigned __int32 IOAddress, unsigned __int16 Value);

private:
    inline bool __fastcall ShouldIndicatePacketToGuest(const EthernetHeader & inEthernetHeader);

    void __fastcall RaiseInterrupt(unsigned __int8 InterruptType);
    void __fastcall ClearInterrupt(void);

    union {    // this structure matches the FCR_* constants in cardserv.h
        struct {
            unsigned __int8 FCSR_INTR_ACK:1;    // 01 - interrupt acknowledge
            unsigned __int8 FCSR_INTR:1;        // 02 - interrupt pending
            unsigned __int8 FCSR_PWR_DOWN:1;    // 04 - place PC card in power down mode
            unsigned __int8 FCSR_AUDIO:1;        // 08 - enable audio signal on BVD2 (pin 62)
            unsigned __int8 Reserved1:1;        // 10
            unsigned __int8 FCSR_IO_IS_8:1;        // 20 - used by host to indicate 8 bit only I/O
            unsigned __int8 FCSR_STSCHG:1;        // 40 - Enable status change (STSCHG, pin 63) from PC card
            unsigned __int8 FCSR_CHANGED:1;        // 80 - Set if one of the status changed bits is set in
                                                //      the pin replacement register.
        } Bits;
        unsigned __int8 Byte;
    } FCSR;

    typedef union {
        struct {
            unsigned __int8 CR_STOP:1;            // 1
            unsigned __int8 CR_START:1;            // 2
            unsigned __int8 CR_XMIT:1;            // 4
            unsigned __int8 CR_DMA_READ:1;        // 8
            unsigned __int8 CR_DMA_WRITE:1;        // 10
            unsigned __int8 CR_NO_DMA:1;        // 20
            unsigned __int8 PageNumber:2;        // CR_PS0 and CR_PS1 together.
        } Bits;
        unsigned __int8 Byte;
    } NIC_COMMAND_Register;    
    NIC_COMMAND_Register NIC_COMMAND; // offset 0

    unsigned __int8 NIC_PAGE_START;    // offset 1
    unsigned __int8 NIC_PHYS_ADDR[6]; // offsets 1-6, all in page1

    unsigned __int8 NIC_PAGE_STOP;    // offset 2

    unsigned __int8 NIC_BOUNDARY;    // offset 3

    union {
        struct {
            unsigned __int8 TSR_XMIT_OK:1;        // 1
            unsigned __int8 Reserved1:1;
            unsigned __int8 TSR_COLLISION:1;    // 4
            unsigned __int8 TSR_ABORTED:1;        // 8
            unsigned __int8 TSR_NO_CARRIER:1;    // 10
            unsigned __int8 Reserved2:1;
            unsigned __int8 TSR_NO_CDH:1;        // 40
            unsigned __int8 Reserved3:1;
        } Bits;
        unsigned __int8 Byte;
    } NIC_XMIT_STATUS;                // offset 4
    unsigned __int8 NIC_XMIT_START;

    unsigned __int16 NIC_XMIT_COUNT; // offset 5/6
    unsigned __int8 NIC_FIFO;        // offset 6

#define ISR_RCV_BIT 1
#define ISR_XMIT_BIT 2
#define ISR_RCV_ERR_BIT 4
#define ISR_XMIT_ERR_BIT 8
#define ISR_OVERFLOW_BIT 0x10
#define ISR_COUNTER_BIT 0x20
#define ISR_DMA_DONE_BIT 0x40
#define ISR_RESET_BIT 0x80

    union {
        struct {
            unsigned __int8 ISR_RCV:1;            // 1
            unsigned __int8 ISR_XMIT:1;            // 2
            unsigned __int8 ISR_RCV_ERR:1;        // 4
            unsigned __int8 ISR_XMIT_ERR:1;        // 8
            unsigned __int8 ISR_OVERFLOW:1;        // 10
            unsigned __int8 ISR_COUNTER:1;        // 20
            unsigned __int8 ISR_DMA_DONE:1;        // 40
            unsigned __int8 ISR_RESET:1;        // 80
        } Bits;
        unsigned __int8 Byte;
    } NIC_INTR_STATUS; // offset 7/8
    unsigned __int8 NIC_CURRENT;
    unsigned __int8 NIC_MC_ADDR[8];    // offsets 8-f, the multicast address

    unsigned __int16 NIC_CRDA;        // offset 8/9
    unsigned __int16 NIC_RMT_ADDR;

    unsigned __int16 NIC_RMT_COUNT; // offset a/b

    union {
        struct {
            unsigned __int8 Reserved1:2;
            unsigned __int8 RCR_BROADCAST:1;    // 4
            unsigned __int8 RCR_MULTICAST:1;    // 8
            unsigned __int8 RCR_ALL_PHYS:1;        // 10
            unsigned __int8 RCR_MONITOR:1;        // 20
            unsigned __int8 Reserved2:2;
        } Bits;
        unsigned __int8 Byte;
    } NIC_RCV_CONFIG; // offset c
    union {
        struct {
            unsigned __int8 RSR_PACKET_OK:1;    // 1
            unsigned __int8 RSR_CRC_ERROR:2;    // 2
            unsigned __int8 Reserved1:3;
            unsigned __int8 RSR_MULTICAST:1;    // 20
            unsigned __int8 RSR_DISABLED:1;        // 40
            unsigned __int8 RSR_DEFERRING:1;    // 80
        } Bits;
        unsigned __int8 Byte;
    } NIC_RCV_STATUS;

    union {
        struct {
            unsigned __int8 TCR_INHIBIT_CRC:1;    // 1
            unsigned __int8 TCR_NIC_LBK:1;        // 2 also called TCR_LOOPBACK
            unsigned __int8 TCR_SNI_LBK:1;        // 4
            unsigned __int8 Reserved1:5;
        } Bits;
        unsigned __int8 Byte;
    } NIC_XMIT_CONFIG; // offset d
    unsigned __int8 NIC_FAE_ERR_CNTR;

    union {
        struct {
            unsigned __int8 DCR_WORD_WIDE:1;    // 1
            unsigned __int8 Reserved1:2;
            unsigned __int8 DCR_NORMAL:1;        // 8:  1 means normal operation, 0 means loopback
            unsigned __int8 DCR_AUTO_LIMIT:1;    // 10
            unsigned __int8 DCR_FIFO_SIZE:2;    // 20 and 40
            unsigned __int8 Reserved2:1;
        } Bits;
        unsigned __int8 Byte;
    } NIC_DATA_CONFIG; // offset e
    unsigned __int8 NIC_CRC_ERR_CNTR;

    union {
        struct {
            unsigned __int8 IMR_RCV:1;            // 1
            unsigned __int8 IMR_XMIT:1;            // 2
            unsigned __int8 IMR_RCV_ERR:1;        // 4
            unsigned __int8 IMR_XMIT_ERR:1;        // 8
            unsigned __int8 IMR_OVERFLOW:1;        // 10
            unsigned __int8 IMR_COUNTER:1;        // 20
            unsigned __int8 Reserved1:2;
        } Bits;
        unsigned __int8 Byte;
    } NIC_INTR_MASK; // offset f
    unsigned __int8 NIC_MISSED_CNTR;

    static const unsigned __int8 CISData[];

    static const unsigned __int32 RomSize=32;
    unsigned __int8 CardROM[RomSize];

    bool fHasBeenPoweredOnBefore;
    VPCNetDriver VPCNet;
    unsigned __int16 DMACount;
    unsigned __int16 DMAOffset;

    unsigned __int8 ReceiveBuffer[2048];
    void BeginAsyncReceive(void);
    COMPLETIONPORT_CALLBACK ReceiveCallback;
    static void ReceiveCompletionRoutineStatic(LPVOID lpParameter, DWORD dwBytesTransferred, LPOVERLAPPED lpOverlapped);
    void ReceiveCompletionRoutine(DWORD dwBytesTransferred);
    int AsyncReceiveCount; // used to ensure that only one async receive is pending at any one time.

    COMPLETIONPORT_CALLBACK TransmitCallback;
    static void TransmitCompletionRoutineStatic(LPVOID lpParameter, DWORD dwBytesTransferred, LPOVERLAPPED lpOverlapped);
    void TransmitCompletionRoutine();

    // CardRAM as at the end for alignment reasons:  it follows a
    // HANDLE datatype to be sure it is at least 4-byte aligned so
    // memcpy/memset are as efficient as possible.
    static const unsigned __int32 RamBase=0x4000;        
    static const unsigned __int32 RamSize=0x4000;
    unsigned __int8 CardRAM[RamSize];
};

