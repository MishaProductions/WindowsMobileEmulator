/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#include "emulator.h"
#include "Config.h"
#include "resource.h"
#include "CompletionPort.h"
#include "VPCNet.h"
#include <winioctl.h>
#include <iphlpapi.h>
#include <setupapi.h>
#include <devguid.h>

// Include the logging infrastructure
#include "dev_emulator_log.h"
#define TMH_FILENAME "vpcnet.tmh"
#include "vsd_logging_inc.h"

// The following defines and types were copied from Connectix's tools\whql\vpcnetts\main.cpp:

#define NETSV_DRIVER_NAME                    L"\\\\.\\VPCNetS2"

#define    kVPCNetSvVersionMajor    2
#define    kVPCNetSvVersionMinor    2
#define    kVPCNetSvVersion        ((kVPCNetSvVersionMajor << 16) | kVPCNetSvVersionMinor)

#define FILE_DEVICE_PROTOCOL                0x8000

#define ETHERNET_ADDRESS_LENGTH 6

enum
{
    kIoctlFunction_SetOid                     = 0,
    kIoctlFunction_QueryOid,
    kIoctlFunction_Reset,
    kIoctlFunction_EnumAdapters,
    kIoctlFunction_GetStatistics,
    kIoctlFunction_GetVersion,
    kIoctlFunction_GetFeatures,
    kIoctlFunction_SendToHostOnly,
    kIoctlFunction_RegisterGuest,
    kIoctlFunction_DeregisterGuest,
    kIoctlFunction_CreateVirtualAdapter,
    kIoctlFunction_DestroyVirtualAdapter,
    kIoctlFunction_GetAdapterAttributes,
    kIoctlFunction_Bogus                     = 0xFFF
};

// These IOCTLs apply only to the control object
#define IOCTL_ENUM_ADAPTERS                    CTL_CODE(FILE_DEVICE_PROTOCOL, kIoctlFunction_EnumAdapters,                METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_GET_VERSION                    CTL_CODE(FILE_DEVICE_PROTOCOL, kIoctlFunction_GetVersion,                METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_GET_FEATURES                    CTL_CODE(FILE_DEVICE_PROTOCOL, kIoctlFunction_GetFeatures,                METHOD_BUFFERED, FILE_ANY_ACCESS)

#define kVPCNetSvFeatureDriverNegotiatesMACAddresses    0x00000004
#define kVPCNetSvFeatureVirtualAdapterSupport        0x00000008

// These IOCTLs apply only to the adapter object
#define IOCTL_PROTOCOL_RESET                CTL_CODE(FILE_DEVICE_PROTOCOL, kIoctlFunction_Reset,                    METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_CREATE_VIRTUAL_ADAPTER        CTL_CODE(FILE_DEVICE_PROTOCOL, kIoctlFunction_CreateVirtualAdapter,        METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_DESTROY_VIRTUAL_ADAPTER        CTL_CODE(FILE_DEVICE_PROTOCOL, kIoctlFunction_DestroyVirtualAdapter,    METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_GET_ADAPTER_ATTRIBUTES        CTL_CODE(FILE_DEVICE_PROTOCOL, kIoctlFunction_GetAdapterAttributes,        METHOD_BUFFERED, FILE_ANY_ACCESS)

// These IOCTLs apply only to the virtual adapter object
#define IOCTL_PROTOCOL_SET_OID                CTL_CODE(FILE_DEVICE_PROTOCOL, kIoctlFunction_SetOid,                    METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_PROTOCOL_QUERY_OID            CTL_CODE(FILE_DEVICE_PROTOCOL, kIoctlFunction_QueryOid,                    METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_SEND_TO_HOST_ONLY                CTL_CODE(FILE_DEVICE_PROTOCOL, kIoctlFunction_SendToHostOnly,            METHOD_IN_DIRECT, FILE_ANY_ACCESS)

#define IOCTL_REGISTER_GUEST                CTL_CODE(FILE_DEVICE_PROTOCOL, kIoctlFunction_RegisterGuest,            METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_DEREGISTER_GUEST                CTL_CODE(FILE_DEVICE_PROTOCOL, kIoctlFunction_DeregisterGuest,            METHOD_BUFFERED, FILE_ANY_ACCESS)

// These IOCTLs apply to both the adapter and virtual adapter object
#define IOCTL_GET_STATISTICS                CTL_CODE(FILE_DEVICE_PROTOCOL, kIoctlFunction_GetStatistics,            METHOD_BUFFERED, FILE_ANY_ACCESS)


// NETSV_REGISTER_GUEST.fFlags constants
#define kVPCNetSvRegisterGuestFlag_AddressActionMask    0x00000003
#define    kVPCNetSvRegisterGuestFlag_GenerateAddress        0x00000000    // The VPCNetSv driver should generate a unique MAC address
#define    kVPCNetSvRegisterGuestFlag_AddressIsSuggested    0x00000001    // The VPCNetSv driver should use the specified address if not already in use - generate a new one otherwise
#define kVPCNetSvRegisterGuestFlag_AddressIsExclusive    0x00000002    // The VPCNetSv driver must use the specified address - fail if not available
#define kVPCNetSvRegisterGuestFlag_AddressMayDuplicate    0x00000003    // The VPCNetSv driver must use the specified address - may duplicate an existing address

#define kVPCNetSvRegisterGuestFlag_AddressWasGenerated    0x80000000    // The VPCNetSv driver generated a new MAC address

#define OID_GEN_CURRENT_PACKET_FILTER            0x0001010E    // Set or Query
#define OID_802_3_CURRENT_ADDRESS               0x01010102    // Query only

#define NDIS_PACKET_TYPE_DIRECTED                0x00000001
#define NDIS_PACKET_TYPE_MULTICAST                0x00000002
#define NDIS_PACKET_TYPE_ALL_MULTICAST            0x00000004
#define NDIS_PACKET_TYPE_BROADCAST                0x00000008
#define NDIS_PACKET_TYPE_PROMISCUOUS            0x00000020

#define DEFAULT_PACKET_FILTER    (NDIS_PACKET_TYPE_DIRECTED + NDIS_PACKET_TYPE_MULTICAST + NDIS_PACKET_TYPE_BROADCAST)


#pragma pack(push)
#pragma pack(1)

typedef struct _PACKET_OID_DATA {

    ULONG            fOid;
    ULONG            fLength;
    UCHAR            fData[1];

}   PACKET_OID_DATA, *PPACKET_OID_DATA;

#define PACKET_OID_DATA_HEADER_LENGTH    sizeof(PACKET_OID_DATA) - sizeof(UCHAR)
#define PACKET_OID_DATA_LENGTH(_p)        (PACKET_OID_DATA_HEADER_LENGTH + (_p)->fLength)

typedef struct _NETSV_CREATE_VIRTUAL_ADAPTER    NETSV_CREATE_VIRTUAL_ADAPTER, *PNETSV_CREATE_VIRTUAL_ADAPTER;
struct _NETSV_CREATE_VIRTUAL_ADAPTER
{
    ULONG            fAdapterID;
};
typedef const struct _NETSV_CREATE_VIRTUAL_ADAPTER    *PCNETSV_CREATE_VIRTUAL_ADAPTER;


typedef struct _NETSV_REGISTER_GUEST    NETSV_REGISTER_GUEST, *PNETSV_REGISTER_GUEST;
struct _NETSV_REGISTER_GUEST
{
    USHORT            fVersion;                                // Version of this structure
    USHORT            fLength;                                // Length of following data
    ULONG            fFlags;                                    // Flags
    UCHAR            fMACAddress[ETHERNET_ADDRESS_LENGTH];    // Guest VM MAC address
};
typedef const struct _NETSV_REGISTER_GUEST    *PCNETSV_REGISTER_GUEST;

#define NETSV_REGISTER_GUEST_VERSION    0x0002
#define    NETSV_REGISTER_GUEST_LENGTH        (sizeof(NETSV_REGISTER_GUEST) - 2*sizeof(USHORT))

#define MAX_ADAPTERS                        256
#define MAX_ADAPTER_BUFFER_SIZE                65536

#pragma pack(pop)

// End of contents copied from tools\whql\vpcnetts\main.cpp

#define MAC_CACHE_NAME L"\\MAC Addresses"
bool OpenMACAddressCacheKey(HKEY *phKey)
{
    return true;
}

bool GetMACAddressCacheKey(__out_ecount(VMIDStringLength) WCHAR *VMIDString, size_t VMIDStringLength)
{
    HRESULT hr;

    hr = StringCchPrintfW(VMIDString, VMIDStringLength, L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        Configuration.VMID.Data1,
        Configuration.VMID.Data2,
        Configuration.VMID.Data3,
        Configuration.VMID.Data4[0],
        Configuration.VMID.Data4[1],
        Configuration.VMID.Data4[2],
        Configuration.VMID.Data4[3],
        Configuration.VMID.Data4[4],
        Configuration.VMID.Data4[5],
        Configuration.VMID.Data4[6],
        Configuration.VMID.Data4[7]);
    if (FAILED(hr)) {
        ASSERT(FALSE);
        return false;
    }
    return true;
}

bool GetCachedMACAddress(USHORT *GuestMACAddress, const wchar_t* DeviceName)
{
    HKEY hKeyCache;
    LONG l;
    HKEY hKeyCacheVMID;
    DWORD cbData;
    WCHAR VMIDString[50];  // large enough for a GUID "{c200e360-38c5-11ce-ae62-08002b2b79ef}" plus "_" and an 8-hex-digit SessionId

    if (!GetMACAddressCacheKey(VMIDString, ARRAY_SIZE(VMIDString))) {
        LOG_ERROR(NETWORK, "Failed to generate registry key");
        return false;
    }
    l = RegOpenKey(HKEY_CURRENT_USER, EMULATOR_REGPATH MAC_CACHE_NAME, &hKeyCache);
    if (l != ERROR_SUCCESS) {
        LOG_ERROR(NETWORK, "Failed to get the emulator registry node with %d", l);
        return false;
    }
    l = RegOpenKey(hKeyCache, VMIDString, &hKeyCacheVMID);
    RegCloseKey(hKeyCache);
    if (l != ERROR_SUCCESS) {
        LOG_INFO(NETWORK, "Didn't find a cached emulator MAC registry node with %d", l);
        return false;
    }
    cbData = 6;
    l = RegQueryValueEx(hKeyCacheVMID, DeviceName, NULL, NULL, (LPBYTE)GuestMACAddress, &cbData);
    RegCloseKey(hKeyCacheVMID);
    if (l != ERROR_SUCCESS || cbData != 6) {
        // API failed or the data is too long or too short to be a MAC addr
        LOG_ERROR(NETWORK, "MAC address is not present or currupted in registry %d", l);
        return false;
    }
    LOG_INFO(NETWORK, "Found a cached guest MAC: %x-%x-%x-%x-%x-%x",
                       ((LPBYTE)GuestMACAddress)[0], ((LPBYTE)GuestMACAddress)[1], 
                       ((LPBYTE)GuestMACAddress)[2], ((LPBYTE)GuestMACAddress)[3], 
                       ((LPBYTE)GuestMACAddress)[4], ((LPBYTE)GuestMACAddress)[5]);
    return true;
}

void SetCachedMACAddress(const USHORT *GuestMACAddress, const wchar_t* DeviceName)
{
    HKEY hKeyCache;
    LONG l;
    HKEY hKeyCacheVMID;
    DWORD dwDisposition;
    WCHAR VMIDString[50];  // large enough for a GUID "{c200e360-38c5-11ce-ae62-08002b2b79ef}" plus "_" and an 8-hex-digit SessionId

    if (!GetMACAddressCacheKey(VMIDString, ARRAY_SIZE(VMIDString))) {
        LOG_ERROR(NETWORK, "Failed to generate registry key");
        return;
    }
    l = RegCreateKeyEx(HKEY_CURRENT_USER, EMULATOR_REGPATH MAC_CACHE_NAME, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKeyCache, &dwDisposition);
    if (l != ERROR_SUCCESS) {
        LOG_ERROR(NETWORK, "Failed to get the emulator registry node with %d", l);
        return;
    }
    l = RegCreateKeyEx(hKeyCache, VMIDString, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKeyCacheVMID, &dwDisposition);
    RegCloseKey(hKeyCache);
    if (l != ERROR_SUCCESS) {
        LOG_ERROR(NETWORK, "Failed to create an entry for key %S", VMIDString);
        return;
    }
    RegSetValueEx(hKeyCacheVMID, DeviceName, 0, REG_BINARY, (const BYTE*)GuestMACAddress, 6);
    RegCloseKey(hKeyCacheVMID);
}

PIP_ADAPTER_INFO getIPAdapterInfo()
{
    // Get the list of adapters with their MAC addresses
    DWORD dwResult = 0;
    bool result = false;
    ULONG outBufLen = sizeof(IP_ADAPTER_INFO)*10;
    PIP_ADAPTER_INFO pAddresses = new IP_ADAPTER_INFO[10];
    if ( pAddresses == NULL )
    {
        LOG_ERROR(NETWORK, "Failed to allocate the initial adapter array");
        goto ErrorExit;
    }

    // Make an initial call to GetAdaptersAddresses with a 10 adapter sized buffer
    if (( dwResult = GetAdaptersInfo(pAddresses, &outBufLen)) != NO_ERROR)
    {
        if ( dwResult == ERROR_BUFFER_OVERFLOW )
        {
            // Allocate a larger buffer
            delete [] pAddresses;
            pAddresses = (IP_ADAPTER_INFO*) new IP_ADAPTER_INFO[outBufLen/sizeof(IP_ADAPTER_INFO)];
            if ( pAddresses == NULL )
            {
                LOG_ERROR(NETWORK, "Failed to allocate the adapter array. Size %d bytes",
                                    outBufLen);
                goto ErrorExit;
            }
                
            // Make a second call to GetAdapters Addresses with a larger buffer
            if (GetAdaptersInfo(pAddresses, &outBufLen) != NO_ERROR)
            {
                LOG_ERROR(NETWORK, "GetAdaptersInfo failed with requested size array");
                goto ErrorExit;
            }
        }
        else
        {
            // The call failed for a reason we can't correct
            LOG_ERROR(NETWORK, "GetAdaptersInfo failed with initial size array");
            goto ErrorExit;
        }
    }

    result = true;

ErrorExit:
    if ( !result && pAddresses != NULL )
    {
        delete [] pAddresses;
        pAddresses = NULL;
    }

    return pAddresses;
}

PIP_ADAPTER_INFO MACtoNetworkGUID(PIP_ADAPTER_INFO pAddresses, unsigned __int8 * HostMACAddress)
{
    PIP_ADAPTER_INFO pAddressCur = pAddresses;

    // Make sure that the MAC address is valid
    if ( HostMACAddress == NULL )
        return NULL;

    // Find out the friendly name which matches the MAC address provided by the user
    while (pAddressCur) 
    {
        // Check if we got a match
        VPCENetMACAddress CurrentAddress((unsigned __int8 *)pAddressCur->Address);
        if ( CurrentAddress.IsEqualTo(HostMACAddress) )
            break;
        // Increment to the next address
        pAddressCur = pAddressCur->Next;
    }

    return pAddressCur;
}

bool InitalizeActiveSyncAdapterList(AdapterListEntry ** outListHead)
{
    // This is the AppCompatId specified for all DTPT capable devices.
    static CHAR const szDTPTAppCompatValue[] = "usb\\class_ef&subclass_01&prot_01";
    HKEY hAdapterKey = (HKEY)INVALID_HANDLE_VALUE;
    HDEVINFO hDevInfoSet = INVALID_HANDLE_VALUE;
    AdapterListEntry * Head = NULL;
    bool result = false;
    *outListHead = NULL;

    hDevInfoSet = SetupDiGetClassDevs(
                &GUID_DEVCLASS_NET,
                NULL,   // Enumerator
                NULL,   // hWndParent
                DIGCF_PRESENT|  // only devices that are present
                DIGCF_PROFILE   // and active in this h/w profile
                );

    if ( hDevInfoSet == INVALID_HANDLE_VALUE)
    {
        LOG_WARN(NETWORK, "SetupDiGetClassDevs Failed 0x%08x",GetLastError());
        goto Exit;
    }

    SP_DEVINFO_DATA     DevInfoData;
    DevInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD iDev = 0;/*break if SetupDiGetClassDevs fails*/;iDev++)
    {
        if (!SetupDiEnumDeviceInfo( hDevInfoSet, iDev, &DevInfoData))
        {
            break; // No More Devices
        }

        // Open RegKey.
        hAdapterKey = SetupDiOpenDevRegKey(
                    hDevInfoSet, &DevInfoData,
                    DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_READ);

        if (hAdapterKey == INVALID_HANDLE_VALUE)
        {
            LOG_WARN(NETWORK, "device %d: OpenDevRegKey failed: 0x%x",iDev, GetLastError());
            continue;
        }

        // For safety null terminate szValue, and report its size not including the NULL.
        char szValue [BUFFER_SIZE_AS_NIC];
        DWORD bValue = sizeof(szValue) - sizeof(szValue[0]);
        szValue[BUFFER_SIZE_AS_NIC - 1] = 0; 

        // Check if AppCompat value indicates that this is an ActiveSync adapter.
        const CHAR szMatchingDeviceId[] = "MatchingDeviceId";
        DWORD ret = RegQueryValueExA(
                    hAdapterKey, szMatchingDeviceId,
                    NULL, NULL, 
                    reinterpret_cast<BYTE*>(szValue), &bValue
                    );

        if (ret != ERROR_SUCCESS)
        {
            LOG_WARN(NETWORK, "RegQueryValueEx failed for DeviceId %x",ret);
            continue;
        }

        C_ASSERT(sizeof(szDTPTAppCompatValue) < sizeof(szValue));

        // szValue and szDTPTAppCompatValue will be null terminated
        // the strncmp (instead of strcmp) is paranoid.
        if (strncmp(szValue,szDTPTAppCompatValue,ARRAY_SIZE(szDTPTAppCompatValue))==0)
        {
            // Get the name for the adapter
            bValue = sizeof(szValue) - sizeof(szValue[0]);
            const CHAR szAdapterNameRegValue[] = "NetCfgInstanceId";  
            ret = RegQueryValueExA(
                    hAdapterKey, szAdapterNameRegValue,
                    NULL, NULL,
                    reinterpret_cast<BYTE*>(szValue), &bValue);

            if (ret != ERROR_SUCCESS)
            {
                LOG_WARN(NETWORK, "RegQueryValueEx For NetCfgInstanceId failed %x",ret);
                continue;
            }

            // Append adapter to the list
            LOG_INFO(NETWORK, "Found ActiveSync adapter - %s", szValue );
            AdapterListEntry * newEntry = new AdapterListEntry();
            if ( newEntry != NULL )
            {
                strncpy_s(newEntry->Name, ARRAY_SIZE(newEntry->Name), szValue, ARRAY_SIZE(szValue));
                newEntry->next = Head;
                Head = newEntry;
            }
            else 
            {
                goto Exit;
            }
        }
    }

    result = true;
    *outListHead = Head;

Exit:
    if ( hDevInfoSet != INVALID_HANDLE_VALUE )
        SetupDiDestroyDeviceInfoList(hDevInfoSet);
    if ( hAdapterKey != INVALID_HANDLE_VALUE )
        RegCloseKey(hAdapterKey);
    if ( !result )
    {
        // Free up the list of active sync adapters if there was an error
        while ( Head != NULL )
        {
            AdapterListEntry * temp = Head;
            Head = Head->next;
            delete temp;
        }
    }
    return result;
}

bool isActiveSyncAdapter(AdapterListEntry * List, PIP_ADAPTER_INFO pAddresses, unsigned __int8 * HostMACAddress)
{
    bool result = false;

    PIP_ADAPTER_INFO pAddressCur = MACtoNetworkGUID(pAddresses, HostMACAddress);
    // If none of the adapters matched the MAC address provided exit with an error
    if (pAddressCur == NULL)
    {
        LOG_WARN(NETWORK, "Failed to find requested NIC: %x-%x-%x-%x-%x-%x while checking for ActiveSync NIC",
                           HostMACAddress[0], HostMACAddress[1], HostMACAddress[2],
                           HostMACAddress[3], HostMACAddress[4], HostMACAddress[5]);
        goto Exit;
    }

    while ( List != NULL )
    {
        DWORD len = __min( ARRAY_SIZE(List->Name), strlen(pAddressCur->AdapterName));
        if (strncmp(List->Name, pAddressCur->AdapterName, len) == 0  )
        {
            LOG_WARN(NETWORK, "Ignoring NIC: %x-%x-%x-%x-%x-%x matched ActiveSync %s",
                     HostMACAddress[0], HostMACAddress[1], HostMACAddress[2],
                     HostMACAddress[3], HostMACAddress[4], HostMACAddress[5],
                     List->Name);
            result = true;
            break;
        }
        List = List->next;
    }

Exit:
    return result;
}

wchar_t * VPCNetDriver::NetworkAdapterOracle(__in CHAR * AdaptersBuffer, ULONG Size, unsigned __int8 * HostMACAddress)
{
    // If there is not enough data to read the count return NULL
    if (Size < sizeof(ULONG))
    {
        LOG_WARN(NETWORK, "There are no network cards on the machine or data is corrupted");
        return NULL;
    }
    // Count the adapters
    unsigned int AdapterCount = *(ULONG *)AdaptersBuffer;

    // If there are no adapters return NULL
    if (AdapterCount == 0)
    {
        LOG_WARN(NETWORK, "There are no network cards on the machine or data is corrupted");
        return NULL;
    }

    // Initalize the adapter pointer to the first entry
    wchar_t * CurrentAdapter = (wchar_t*) &AdaptersBuffer[sizeof(ULONG)];

    // Create an empty MAC address
    __int64 temp_space = 0;
    VPCENetMACAddress EmptyAddress((unsigned __int8 *)&temp_space);

    PIP_ADAPTER_INFO pAddresses = getIPAdapterInfo();
    if ( pAddresses == NULL)
    {
        LOG_ERROR(NETWORK, "getIPAdapterInfo failed to return a list of NICs");
        return NULL;
    }

    // If the user didn't specify an address try to find a connected functional NIC
    if ( EmptyAddress.IsEqualTo(HostMACAddress) )
    {
        LOG_INFO(NETWORK, "We are looking for any connected network card");
        // Declare and initialize variables.
        PMIB_IFTABLE ifTable = NULL;
        DWORD dwSize = 0;
        AdapterListEntry * ActiveSyncList = NULL;
        
        if (!InitalizeActiveSyncAdapterList(&ActiveSyncList))
        {
            delete [] pAddresses;
            LOG_ERROR(NETWORK, "InitalizeActiveSyncAdapterList failed to return a list of NICs");
            return NULL;
        }

        // Allocate memory for our pointers.
        ifTable = (MIB_IFTABLE*) new unsigned __int8[sizeof(MIB_IFTABLE)];

        // Make an initial call to GetIfTable to get the
        // necessary size into dwSize
        if (ifTable != NULL && GetIfTable(ifTable, &dwSize, 0) == ERROR_INSUFFICIENT_BUFFER) 
        {
            delete [] ifTable;
            ifTable = (MIB_IFTABLE*) new unsigned __int8[dwSize];
        }
        // Make a second call to GetIfTable to get the actual
        // data we want.
        if (ifTable != NULL && GetIfTable(ifTable, &dwSize, 0) == NO_ERROR )
        {
            for(unsigned int t_i = 0; t_i < ifTable->dwNumEntries; t_i++ ) 
            {
                #pragma prefast(suppress:26000, "Prefast doesn't understand that we reallocate the buffer")
                if ( ifTable->table[t_i].dwType != MIB_IF_TYPE_LOOPBACK && 
                      /* Explicitly ignore the Magneto ActiveSync adapter */ 
                     (!isActiveSyncAdapter(ActiveSyncList, pAddresses,ifTable->table[t_i].bPhysAddr)) &&
                     ( ifTable->table[t_i].dwOperStatus == MIB_IF_OPER_STATUS_CONNECTED ||
                       ifTable->table[t_i].dwOperStatus == MIB_IF_OPER_STATUS_OPERATIONAL ) &&
                       ifTable->table[t_i].dwPhysAddrLen == 6)
                {
                    for (unsigned int mac_i =0; mac_i < ifTable->table[t_i].dwPhysAddrLen; mac_i++)
                        HostMACAddress[mac_i] = ifTable->table[t_i].bPhysAddr[mac_i];
                    // Log the address of the network card
                    LOG_INFO(NETWORK, "Found a connected NIC: %x-%x-%x-%x-%x-%x",
                            HostMACAddress[0], HostMACAddress[1], HostMACAddress[2],
                            HostMACAddress[3], HostMACAddress[4], HostMACAddress[5]);
                    break;
                }
            }
        }
        if ( ifTable != NULL )
            delete [] ifTable;
        // Free up the list of active sync adapters as it is no longer needed
        while ( ActiveSyncList != NULL )
        {
            AdapterListEntry * temp = ActiveSyncList;
            ActiveSyncList = ActiveSyncList->next;
            delete temp;
        }
    }

    // Even if only one adapter is available we want to make sure it matches the MAC the user requested
    if ( !EmptyAddress.IsEqualTo(HostMACAddress) )
    {
        wchar_t ** FriendlyName = NULL;
        wchar_t * BestMatchAdapter = NULL;
        unsigned int i;

        FriendlyName = new wchar_t * [AdapterCount];
        if ( FriendlyName == NULL )
        {
            LOG_ERROR(NETWORK, "Failed to allocate memory for adapter friendly name array");
            goto ErrorExit;
        }

        // Figure out the friendly names of the adapters the Virtual Switch knows about
        for (i = 0; i < AdapterCount; i++ )
        {
            // From: vpc\source\comp\driver\net\sys\VPCNetSvAdapter.c
            // We will return the data in the following format: 
            // numOfAdapters + One_Or_More("AdapterName\0" + "SymbolicLink\0") + UNICODE_NULL
            //
            FriendlyName[i] = CurrentAdapter;
            // Skip over the friendly name
            CurrentAdapter += wcslen(CurrentAdapter)+1;
            // Skip over the symbolic link and the UNICODE_NULL
            CurrentAdapter += wcslen(CurrentAdapter)+1;
            CurrentAdapter += sizeof(UNICODE_NULL)/sizeof(wchar_t);
            // Guard against buffer overflow
            if ( (unsigned)((CHAR *)CurrentAdapter - AdaptersBuffer) > Size  )
            {
                LOG_ERROR(NETWORK, "Virtual Switch data is corrupted");
                goto ErrorExit;
            }
        }
        PIP_ADAPTER_INFO pAddressCur = MACtoNetworkGUID(pAddresses, HostMACAddress);
        // If none of the adapters matched the MAC address provided exit with an error
        if (pAddressCur == NULL)
        {
            LOG_WARN(NETWORK, "Failed to find requested NIC: %x-%x-%x-%x-%x-%x",
                           HostMACAddress[0], HostMACAddress[1], HostMACAddress[2],
                           HostMACAddress[3], HostMACAddress[4], HostMACAddress[5]);
            goto ErrorExit;
        }

        LOG_INFO(NETWORK, "Found the requested NIC in the machine");

        // Now try to match the GUID of the adapter to the GUID from the virtual switch
        for (i = 0; i < AdapterCount; i++ )
        {
            // Skip over the friendly name to the symbolic link
            wchar_t * AdapterNameW = FriendlyName[i] + wcslen(FriendlyName[i])+1;
            //Find the openning brace of the GUID
            AdapterNameW = wcschr(AdapterNameW, '{');
            //If the GUID is not there exit
            if ( AdapterNameW == NULL )
            {
                LOG_ERROR(NETWORK, "Virtual Switch data is corrupted - %S", FriendlyName[i]);
                goto ErrorExit;
            }

            // Convert the adapter name GUID to a char string
            char AdapterNameM[40];
            size_t char_conv = 0;
            if (
              wcstombs_s( &char_conv, AdapterNameM, 40, AdapterNameW, 40 ) ||
              char_conv == (size_t)-1 )
            {
                LOG_ERROR(NETWORK, "Conversion of the GUID failed %S", AdapterNameW);
                goto ErrorExit;
            }

            // Compare the GUIDs
            if (strncmp(pAddressCur->AdapterName, AdapterNameM, wcslen(AdapterNameW)) == 0) 
            {
                LOG_INFO(NETWORK, "Found Virt. Switch entry for NIC - %S", FriendlyName[i]);
                BestMatchAdapter = FriendlyName[i];
                break;
            }
        }

        ErrorExit:
        if ( FriendlyName != NULL )
            delete [] FriendlyName;
        if ( pAddresses != NULL )
            delete [] pAddresses;
        if ( BestMatchAdapter == NULL)
        {
            LOG_WARN(NETWORK, "NetworkOracle failed to find a NIC");
            return NULL;
        }
        else
            CurrentAdapter = BestMatchAdapter;
    }

    // At this point, CurrentAdapter points to the friendly name of the host network adapter.
    // We don't need it, so just skip over it.  This is a good place to add instrumentation
    // if the emulator isn't binding appropriately on multi-homed machines.
    CurrentAdapter += wcslen(CurrentAdapter)+1;

    // Replace the "\DosDevices\" prefix by "\\.\" so Win32 can open the name.
    if (wcsncmp(CurrentAdapter, L"\\DosDevices", 11) != 0) {
        ASSERT(FALSE);  // The name isn't prefixed by \DosDevices
        return NULL;
    }
    CurrentAdapter[10]='.';
    CurrentAdapter[9]='\\';
    CurrentAdapter[8]='\\';

    // Skip to the backslash
    CurrentAdapter +=8;

    return CurrentAdapter;
}



#pragma prefast(suppress:262, "We know that this function does not exceed stack threshold")
bool __fastcall VPCNetDriver::PowerOn(
                                      USHORT* GuestMACAddress, 
                                      const wchar_t* DeviceName, 
                                      unsigned __int8 * HostMACAddress,
                                      COMPLETIONPORT_CALLBACK *pCallersTransmitCallback,
                                      COMPLETIONPORT_CALLBACK *pCallersReceiveCallback)
{
    HANDLE hControl;
    HANDLE hAdapter;
    ULONG BytesReturned;
    ULONG Features;
    wchar_t *AdapterName;
    ULONG Version;
    CHAR AdaptersBuffer[MAX_ADAPTER_BUFFER_SIZE];
    NETSV_CREATE_VIRTUAL_ADAPTER createAdapter;
    WCHAR AdapterDeviceName[_MAX_PATH];
    NETSV_REGISTER_GUEST guestInfo;
    bool UseSuggestedAddress;
    HRESULT hr;

    FirstPacketSent = false;

    ASSERT(PACKET_TYPE_DIRECTED == NDIS_PACKET_TYPE_DIRECTED);
    ASSERT(PACKET_TYPE_MULTICAST == NDIS_PACKET_TYPE_MULTICAST);
    ASSERT(PACKET_TYPE_ALL_MULTICAST == NDIS_PACKET_TYPE_ALL_MULTICAST);
    ASSERT(PACKET_TYPE_BROADCAST == NDIS_PACKET_TYPE_BROADCAST);
    ASSERT(PACKET_TYPE_PROMISCUOUS == NDIS_PACKET_TYPE_PROMISCUOUS);

    hControl = CreateFile(NETSV_DRIVER_NAME, 
                          GENERIC_READ | GENERIC_WRITE, 
                          0, NULL, 
                          OPEN_EXISTING, 
                          0, NULL);
    if (hControl == INVALID_HANDLE_VALUE) {
        LOG_ERROR(NETWORK, "CreateFile on %S failed", NETSV_DRIVER_NAME);
        ShowDialog(ID_MESSAGE_FAILED_VPC_NETWORK_OPEN);
        return false;
    }

    if (DeviceIoControl(hControl, 
                        (DWORD)IOCTL_GET_VERSION,
                        NULL, 0,
                        &Version, sizeof(Version),
                        &BytesReturned,
                        NULL) == FALSE) {
        LOG_ERROR(NETWORK, "IOCTL_GET_VERSION failed");
        CloseHandle(hControl);
        return false;
    }
    if (Version < kVPCNetSvVersion) {
        LOG_ERROR(NETWORK, "Incorrect Virt. Switch version %d", Version);
        ShowDialog(ID_MESSAGE_INVALID_VPC_NETWORK_VERSION, Version, kVPCNetSvVersion);
        return false;
    }

    if (DeviceIoControl(hControl,
                        (DWORD)IOCTL_GET_FEATURES,
                        NULL, 0,
                        &Features, sizeof(Features),
                        &BytesReturned,
                        NULL) == FALSE) {
        LOG_ERROR(NETWORK, "IOCTL_GET_FEATURES failed");
        CloseHandle(hControl);
        return false;
    }

    if ((Features & (kVPCNetSvFeatureDriverNegotiatesMACAddresses|kVPCNetSvFeatureVirtualAdapterSupport)) != (kVPCNetSvFeatureDriverNegotiatesMACAddresses|kVPCNetSvFeatureVirtualAdapterSupport)) {
        LOG_ERROR(NETWORK, "Incorrect Virt. Switch features %d", Features);
        ShowDialog(ID_MESSAGE_INVALID_VPC_NETWORK_FEATURES);
        CloseHandle(hControl);
        return false;
    }

    // Enumerate the list of ethernet adapters exposed from the control device
    if (DeviceIoControl(hControl,
                        (DWORD)IOCTL_ENUM_ADAPTERS,
                        NULL, 0,
                        AdaptersBuffer, sizeof(AdaptersBuffer),
                        &BytesReturned,
                        NULL) == FALSE) {
        LOG_ERROR(NETWORK, "IOCTL_ENUM_ADAPTERS failed");
        CloseHandle(hControl);
        return false;
    }

    AdapterName = NetworkAdapterOracle( AdaptersBuffer, BytesReturned, HostMACAddress);

    if ( AdapterName == NULL ) {
        LOG_ERROR(NETWORK, "NetworkOracle failed to find a NIC");
        ShowDialog(ID_MESSAGE_FAILED_VPC_NETWORK_ADAPTER);
        CloseHandle(hControl);
        return false;
    }

    hAdapter = CreateFile(AdapterName, 
                          GENERIC_READ | GENERIC_WRITE, 
                          0, NULL, 
                          OPEN_EXISTING, 
                          0, NULL);
    if (hAdapter == INVALID_HANDLE_VALUE) {
        LOG_ERROR(NETWORK, "CreateFile on %S failed", AdapterName);
        CloseHandle(hControl);
        return false;
    }

    // Create an adapter instance
    createAdapter.fAdapterID=0;
    if (DeviceIoControl(hAdapter,
                        (DWORD)IOCTL_CREATE_VIRTUAL_ADAPTER,
                        &createAdapter, sizeof(createAdapter),
                        &createAdapter, sizeof(createAdapter),
                        &BytesReturned,
                        NULL) == FALSE) {
        LOG_ERROR(NETWORK, "IOCTL_CREATE_VIRTUAL_ADAPTER failed");
        CloseHandle(hAdapter);
        CloseHandle(hControl);
        return false;
    }

    // Open the newly-created adapter instance
    hr = StringCchPrintfW(AdapterDeviceName, ARRAY_SIZE(AdapterDeviceName), L"%s_%08lX", AdapterName, createAdapter.fAdapterID);
    if (FAILED(hr)) {
        LOG_ERROR(NETWORK, "Failed to create adapter name");
        CloseHandle(hAdapter);
        CloseHandle(hControl);
        return false;
    }
    hVirtualAdapter = CreateFile(AdapterDeviceName, 
                                 GENERIC_READ | GENERIC_WRITE, 
                                 0, NULL, 
                                 OPEN_EXISTING, 
                                 FILE_FLAG_OVERLAPPED, NULL);
    if (hVirtualAdapter == INVALID_HANDLE_VALUE) {
        LOG_ERROR(NETWORK, "CreateFile on %S failed", AdapterDeviceName);
        CloseHandle(hAdapter);
        CloseHandle(hControl);
        return false;
    }

    // Register the guest OS with the driver
    UseSuggestedAddress = (GuestMACAddress[0] != 0 || GuestMACAddress[1] != 0 || GuestMACAddress[2] != 0);
    if (!UseSuggestedAddress) {
        LOG_INFO(NETWORK, "No suggested MAC Address, looking in the registry");
        // The caller doesn't have a suggested MAC address (which would have come
        // from a restore-state).  See if there is a cached MAC address corresponding to
        // this VMID.
        if (GetCachedMACAddress(GuestMACAddress, DeviceName)) {
            UseSuggestedAddress = true;
        }
    }

    if (UseSuggestedAddress) {
        // Most likely a restore-state scenario.  The MAC address is advisory:  don't
        // fail to register if the MAC address is invalid or already in use... instead,
        // allocate a new one.
        LOG_INFO(NETWORK, "Using suggested MAC Address");
        guestInfo.fFlags = kVPCNetSvRegisterGuestFlag_AddressIsSuggested;
        memcpy(guestInfo.fMACAddress, GuestMACAddress, ETHERNET_ADDRESS_LENGTH);
    } else {
        LOG_INFO(NETWORK, "Asking for a new MAC Address");
        guestInfo.fFlags = kVPCNetSvRegisterGuestFlag_GenerateAddress;
        memset(guestInfo.fMACAddress, 0, ETHERNET_ADDRESS_LENGTH);
    }

    bool manuallyGenMAC = false;
RetryRegisterGuest:
    guestInfo.fVersion     = NETSV_REGISTER_GUEST_VERSION;
    guestInfo.fLength     = NETSV_REGISTER_GUEST_LENGTH;
    if (DeviceIoControl(hVirtualAdapter,
                        (DWORD)IOCTL_REGISTER_GUEST,
                        &guestInfo, sizeof(guestInfo),
                        &guestInfo, sizeof(guestInfo),
                        &BytesReturned,
                        NULL) == FALSE) {
        LOG_INFO(NETWORK, "DeviceIoControl failed with IOCTL_REGISTER_GUEST");
        // REGISTER_GUEST fails on a loopback and bridge adapters because it can't
        // generate a guest MAC address. Generate a MAC address manually and retry
        if (!UseSuggestedAddress)
        {
            UseSuggestedAddress = true;
            guestInfo.fFlags = kVPCNetSvRegisterGuestFlag_AddressIsSuggested;
            // Generated a our own address
            HCRYPTPROV   hCryptProv = NULL;
            // Acquire a cryptographic provider context handle.
            if(::CryptAcquireContext( &hCryptProv, NULL, NULL, PROV_RSA_FULL, 0))
            {
                *((int *)guestInfo.fMACAddress) = 0xFFFF0300; // initialize the first 3 bytes of the MAC
                // Fill in the last three bytes with random data
                if (::CryptGenRandom( hCryptProv, 3, &(guestInfo.fMACAddress[ 3 ]) ) )
                    manuallyGenMAC = true;
            }
            else
            {
                DWORD error = GetLastError();
                LOG_ERROR(NETWORK, "Failed to acquire crypto context while generating MAC - %x", error);
                ASSERT(false && "Failed to obtain a cryptographic provider");
            }

            if(hCryptProv)
                CryptReleaseContext(hCryptProv, 0);
            if(manuallyGenMAC)
            {
                LOG_WARN(NETWORK, "Manually generated a MAC address: %x-%x-%x-%x-%x-%x",
                                   guestInfo.fMACAddress[0], guestInfo.fMACAddress[1], 
                                   guestInfo.fMACAddress[2], guestInfo.fMACAddress[3], 
                                   guestInfo.fMACAddress[4], guestInfo.fMACAddress[5]);
                goto RetryRegisterGuest;
            }
        }
        LOG_ERROR(NETWORK, "Failed to manually generate a MAC");
        CloseHandle(hVirtualAdapter);
        CloseHandle(hAdapter);
        CloseHandle(hControl);
        return false;
    }
    if (guestInfo.fFlags & kVPCNetSvRegisterGuestFlag_AddressWasGenerated || manuallyGenMAC) {
        memcpy(GuestMACAddress, guestInfo.fMACAddress, ETHERNET_ADDRESS_LENGTH);
        SetCachedMACAddress(GuestMACAddress, DeviceName);
    }

    if (!ConfigurePacketFilter(DEFAULT_PACKET_FILTER)) {
        LOG_ERROR(NETWORK, "ConfigurePacketFilter failed");
        CloseHandle(hVirtualAdapter);
        CloseHandle(hAdapter);
        CloseHandle(hControl);
        return false;
    }

    Callback.lpRoutine = CompletionRoutineStatic;
    Callback.lpParameter = this;
    if (!CompletionPort.AssociateHandleWithCompletionPort(hVirtualAdapter, &Callback)) {
        LOG_ERROR(NETWORK, "Failed to associate the VPCNET handle with the IO completion port");
        CloseHandle(hVirtualAdapter);
        CloseHandle(hAdapter);
        CloseHandle(hControl);
        return false;
    }
    pTransmitCallback = pCallersTransmitCallback;
    pReceiveCallback = pCallersReceiveCallback;

    LOG_INFO(NETWORK, "Successfully powered on the NIC");
    CloseHandle(hControl);
    return true;
}

bool __fastcall VPCNetDriver::PowerOff(void)
{
    LOG_VERBOSE(NETWORK, "Powering off the NIC");
    CloseHandle(hVirtualAdapter);
    CloseHandle(hAdapter);
    return true;
}

// This transmits a network packet asynchronously.  Call WaitForTransmissionToComplete() to block
// until the transmission completes.
bool __fastcall VPCNetDriver::BeginAsyncTransmitPacket(unsigned __int32 PacketLength, unsigned __int8* PacketData)
{
    BOOL                                        writeCompleted;
    DWORD                                        ignore;

    if ( !Configuration.getHostOnlyRouting() )
    {
        LOG_VVERBOSE(NETWORK, "Transmitting packet length %d", PacketLength);
        writeCompleted = WriteFile(hVirtualAdapter, PacketData, PacketLength, NULL, &OverlappedTransmission);
    }
    else
    {
        // This is counterintuitive, but it's how Microsoft designed it.
        // Since the IOCTL_SEND_TO_HOST_ONLY function code uses METHOD_IN_DIRECT
        // we must pass the packet buffer and length in the output parameters
        // of the DeviceIoControl call. This way the DeviceManager will pass
        // an MDL of the packet buffer to our driver. We can then send the
        // packet to the host.
        writeCompleted = DeviceIoControl(hVirtualAdapter,
                                        (DWORD)IOCTL_SEND_TO_HOST_ONLY,
                                        NULL,
                                        0,
                                        PacketData,
                                        PacketLength,
                                        &ignore,
                                        &OverlappedTransmission);
    }

    if ( !writeCompleted )
    {
        DWORD ErrorStatus = GetLastError();
        if (ErrorStatus != ERROR_IO_PENDING) {
            LOG_ERROR(NETWORK, "WriteFile failed during transmit with %d", ErrorStatus);
            return false;
        }
    }

    if ( !FirstPacketSent )
    {
        FirstPacketSent = true;
        CodeMarker(perfEmulatorBootEnd);
    }

    return true;
}

bool __fastcall VPCNetDriver::BeginAsyncReceivePacket(unsigned __int8* PacketData, unsigned __int16 PacketSize)
{
    ASSERT(InterlockedIncrement(&AsyncReceivesOutstanding) == 1);

    if (ReadFile(hVirtualAdapter, PacketData, PacketSize, NULL, &OverlappedReceipt) == FALSE) {
        DWORD ErrorCode =  GetLastError();
        if (ErrorCode != ERROR_IO_PENDING) {
            LOG_ERROR(NETWORK, "ReadFile failed while recv %d", ErrorCode);
            ASSERT(InterlockedDecrement(&AsyncReceivesOutstanding) == 0);
            return false;
        }
    }

    return true;
}

void VPCNetDriver::CompletionRoutineStatic(LPVOID lpParameter, DWORD dwBytesTransferred, LPOVERLAPPED lpOverlapped)
{
    VPCNetDriver *pThis = (VPCNetDriver*)lpParameter;

    if (lpOverlapped == &pThis->OverlappedTransmission) {
        pThis->pTransmitCallback->lpRoutine(pThis->pTransmitCallback->lpParameter, dwBytesTransferred, lpOverlapped);
    } else if (lpOverlapped == &pThis->OverlappedReceipt) {
        ASSERT(InterlockedDecrement(&pThis->AsyncReceivesOutstanding) == 0);

        pThis->pReceiveCallback->lpRoutine(pThis->pReceiveCallback->lpParameter, dwBytesTransferred, lpOverlapped);
    } else {
        ASSERT(FALSE);
    }
}

bool __fastcall VPCNetDriver::ConfigurePacketFilter(int FilterValue)
{
    DWORD BytesReturned;
    UCHAR oidBuf[PACKET_OID_DATA_HEADER_LENGTH + sizeof(ULONG)];
    PPACKET_OID_DATA oidData;

    // Set up the default packet filter, to enable broadcast packets
    oidData = reinterpret_cast<PPACKET_OID_DATA>(oidBuf);
    oidData->fOid = OID_GEN_CURRENT_PACKET_FILTER;
    oidData->fLength = sizeof(ULONG);
    *((PULONG)oidData->fData) = FilterValue;
    if (DeviceIoControl(hVirtualAdapter,
                        (DWORD)IOCTL_PROTOCOL_SET_OID,
                        oidData,
                        sizeof(oidBuf),
                        oidData,
                        sizeof(oidBuf),
                        &BytesReturned,
                        NULL) == FALSE) {
        return false;
    }
    return true;
}
