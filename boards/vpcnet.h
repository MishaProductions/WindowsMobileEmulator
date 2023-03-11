/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

// Basic header structures from vpc\source\vm\common\network\VPCEthernetTypes.h

class VPCENetMACAddress
{
public:
    // Constructors
    VPCENetMACAddress()
    {
        addr[0] = addr[1] = addr[2] = addr[3] = addr[4] = addr[5] = 0;
    }
    
    VPCENetMACAddress(
        const VPCENetMACAddress & inAddr)
    {
        * reinterpret_cast<unsigned __int32 *>(&addr[0]) =  * reinterpret_cast<const unsigned __int32 *>(&inAddr.addr[0]);
        * reinterpret_cast<unsigned __int16 *>(&addr[4]) =  * reinterpret_cast<const unsigned __int16 *>(&inAddr.addr[4]);
    }

    VPCENetMACAddress( const unsigned __int8 *    inAddr)
    {
        ASSERT(inAddr != NULL);
        * reinterpret_cast<unsigned __int32 *>(&addr[0]) =  * reinterpret_cast<const unsigned __int32 *>(&inAddr[0]);
        * reinterpret_cast<unsigned __int16 *>(&addr[4]) =  * reinterpret_cast<const unsigned __int16 *>(&inAddr[4]);
        
    }

    inline unsigned __int8
    operator[] (int index) const
    {
        ASSERT(index < sizeof(addr));
        return addr[index];
    }

    // Informational accessors
    inline bool 
    IsMulticast(void) const
    {
        return (addr[0] &0x01) != 0;
    }

    inline bool
    IsNotMulticast( void ) const
    {
        return (addr[0] & 0x01) == 0;
    }

    inline bool
    IsBroadcast(void) const
    {
        return ((*reinterpret_cast<const unsigned __int32 *>(&addr[0]) == 0xFFFFFFFF) &&
                (*reinterpret_cast<const unsigned __int16 *>(&addr[4]) == 0xFFFF));
    }

    inline bool
    IsEqualTo (const unsigned __int8 * rhs) const
    {
        return ((*reinterpret_cast<const unsigned __int32 *>(&addr[0]) == *reinterpret_cast<const unsigned __int32 *>(&rhs[0])) &&
                (*reinterpret_cast<const unsigned __int16 *>(&addr[4]) == *reinterpret_cast<const unsigned __int16 *>(&rhs[4])));
    }
private:
    unsigned __int8    addr[6];
};

// The following data structures must be packed.
#pragma pack(1)

struct EthernetHeader
{
    VPCENetMACAddress        fDestinationAddress;
    VPCENetMACAddress        fSourceAddress;
    unsigned __int16        fFrameType;
};

#pragma pack()

#define kCRC32_Poly    0xEDB88320

class VPCNetDriver {
public:
    bool __fastcall PowerOn(USHORT* GuestMACAddress, 
                            const wchar_t* DeviceName, 
                            unsigned __int8 * HostMACAddress,
                            COMPLETIONPORT_CALLBACK *pCallersTransmitCallback,
                            COMPLETIONPORT_CALLBACK *pCallersReceiveCallback);
    bool __fastcall PowerOff(void);
    bool __fastcall BeginAsyncTransmitPacket(unsigned __int32 PacketLength, unsigned __int8* PacketData);
    bool __fastcall BeginAsyncReceivePacket(unsigned __int8* PacketData, unsigned __int16 PacketSize);

    static const int PACKET_TYPE_DIRECTED        =0x00000001;
    static const int PACKET_TYPE_MULTICAST        =0x00000002;
    static const int PACKET_TYPE_ALL_MULTICAST    =0x00000004;
    static const int PACKET_TYPE_BROADCAST        =0x00000008;
    static const int PACKET_TYPE_PROMISCUOUS    =0x00000020;

    bool __fastcall ConfigurePacketFilter(int FilterValue);

private:
    HANDLE hAdapter;
    HANDLE hVirtualAdapter;
    OVERLAPPED OverlappedTransmission;
    OVERLAPPED OverlappedReceipt;
    bool FirstPacketSent;
    COMPLETIONPORT_CALLBACK Callback;

    wchar_t * NetworkAdapterOracle(__in CHAR * AdaptersBuffer, ULONG Size, unsigned __int8 * HostMACAddress);
    static void CompletionRoutineStatic(LPVOID lpParameter, DWORD dwBytesTransferred, LPOVERLAPPED lpOverlapped);
    COMPLETIONPORT_CALLBACK *pReceiveCallback;
    COMPLETIONPORT_CALLBACK *pTransmitCallback;
#ifdef DEBUG
    LONG AsyncReceivesOutstanding;
#endif
};

#define BUFFER_SIZE_AS_NIC 50
struct AdapterListEntry {
    char Name[BUFFER_SIZE_AS_NIC];
    AdapterListEntry * next;
};
