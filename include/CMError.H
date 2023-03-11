/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef __CONMAN_HRESULTS__
#define __CONMAN_HRESULTS__

/*********************************************************************

These HRESULTs are used for mapping managed exceptions to COM error codes
and vice versa through COM Interop.  For background on COM error codes see 
http://msdn.microsoft.com/library/default.asp?url=/library/en-us/com/error_9td2.asp.
 
FACILITY_CONMAN is defined as 0x972 (0x...).
 
Within that range, the following subranges have been allocated for different 
feature areas:

0x10yy for common error codes
0x11yy for Embedded Debug Engine
0x12yy for Embedded Port Supplier
0x13yy for 
0x14yy for 
0x15yy for 
0x16yy for

*********************************************************************/

#include <winerror.h>

#ifndef FACILITY_CONMAN
#define FACILITY_CONMAN            0x973  //TODO: Find the correct number to put here.
#endif

#ifndef CM_EMAKEHR
#define CM_SMAKEHR(val)            MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_CONMAN, val)
#define CM_EMAKEHR(val)            MAKE_HRESULT(SEVERITY_ERROR, FACILITY_CONMAN, val)
#else
#error CM_EMAKEHR must not be defined!
#endif

// Errors that map the AD7 errors. Careful that these map to the definitions
// in msdbg.idl.
#define E_AD7_DUMP_CORRUPTED MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x200 + 36)
#define E_AD7_DEVICEBITS_NOT_SIGNED MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x401)
#define E_AD7_ATTACH_NOT_ENABLED MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x402)
#define E_AD7_REMOTE_DISCONNECT MAKE_HRESULT(SEVERITY_ERROR, FACILITY_ITF, 0x403)

//**** Common Errors 
#define CM_E_FIRST                  CM_EMAKEHR(0x1000)  //First
#define CM_E_TERMINATED_THREAD CM_EMAKEHR(0x1000)  //A non-responsive thread was terminated.
#define CM_E_ABANDONED_WAIT CM_EMAKEHR(0x1001)  //A non-responsive thread was terminated.
#define CM_E_OPERATION_CANCELED CM_EMAKEHR(0x1002)  //
#define CM_E_FAIL_UNEXPECTED_UNDESIREABLE CM_EMAKEHR(0x1003)  //
#define CM_E_ITEM_NOT_PRESENT CM_EMAKEHR(0x1004)  //Item not found in a list or array.
#define CM_E_DLL_UNINITIALIZED CM_EMAKEHR(0x1005)  //Uninit has been called on the DLL - not longer valid to use.
#define CM_E_LAST                  CM_EMAKEHR(0x10FF)  //Last

//**** Embedded Debug Engine
#define EDE_E_FIRST            CM_EMAKEHR(0x1100)
#define EDE_E_UNKNOWN_DEBUG_EVENT CM_EMAKEHR(0x1101)  //Unknown Debug Event Code
#define EDE_E_PROCESS_EXITED CM_EMAKEHR(0x1102)  //The process has exited.
#define EDE_E_PROCESS_NOT_DEBUGGED CM_EMAKEHR(0x1103)  //The process is not under debug.
#define EDE_E_PROCESS_ALREADY_DEBUGGED CM_EMAKEHR(0x1104)  //The process is already under debug - Unable to attach
#define EDE_E_EVENT_TIMEOUT CM_EMAKEHR(0x1105)  //GetNext timed out.
#define EDE_E_EVENT_MISSING CM_EMAKEHR(0x1106)  //Error-number of events inconsistent
#define EDE_E_PROCESS_ACCESS CM_EMAKEHR(0x1107)  //There isn't a process handle yet, and can't get a handle
#define EDE_E_UNSUPPORTED CM_EMAKEHR(0x1108)  //Unsupported feature as called.
#define EDE_E_PROCESS_TERMINATED CM_EMAKEHR(0x1109)  //The process was terminated by the debugger.
#define EDE_E_UNEXPECTED CM_EMAKEHR(0x110A)  //Unexpected error
#define EDE_E_THREAD_NOT_STOPPED CM_EMAKEHR(0x110B)  //Attempting to continue a thread which is not stopped.
#define EDE_E_NO_DEBUG_PERMISSION CM_EMAKEHR(0x110C)  //This means we are unable to debug the process, probably due to unsigned or improperly signed debugger bits.
#define EDE_E_NO_THREADS_SUSPENDABLE CM_EMAKEHR(0x110D)  //No threads are suspendable at this time.
#define EDE_E_EMPTY_STRING CM_EMAKEHR(0x110E)  //Empty string using ReadProcessMemory.
#define EDE_E_LAST            CM_EMAKEHR(0x11FF)

//**** Embedded Port Supplier

#define EPS_E_FIRST            CM_EMAKEHR(0x1200)

#define EPS_E_SNAPSHOT_INVALID CM_EMAKEHR(0x1200)  //The process snapshot called on is valid.
#define EPS_E_INVALID_PROCESS_ENTRY CM_EMAKEHR(0x1201)  //The entry in the registry is not valid for the NETCF process.
#define EPS_E_SNAPSHOT_CANCELED CM_EMAKEHR(0x1202)  //The snapshot was canceled - stop using it.
#define EPS_E_INVALID_PROCESS_ARRAY CM_EMAKEHR(0x1203)  //The process array was not the lenght is was supposed to be
#define EPS_E_INSUFFICIENT_RPC_BUFFER CM_EMAKEHR(0x1204)  //The process array was not the lenght is was supposed to be
#define EPS_E_UNEXPECTED CM_EMAKEHR(0x1205)  //Unexpected Error
#define EPS_E_RPC_LAYER CM_EMAKEHR(0x1206)  //RPC related failure
#define EPS_E_BAD_THREAD_ID CM_EMAKEHR(0x1207)  //Thread ID Invalid or not longer exists
#define EPS_E_NO_TOOLHELP_HANDLES CM_EMAKEHR(0x1208)  //Too many open Toolhelp handles - stress test or a leak.
#define EPS_E_BAD_RPC_DLL CM_EMAKEHR(0x1209)  //RPC DLL doesn't expose all the expected methods.
#define EPS_E_NO_DLL_NAME CM_EMAKEHR(0x120A)  //Unable to fetch the DLL name
#define EPS_E_TOOLHELP_EXCEPTION CM_EMAKEHR(0x120B)  //Exception calling a toolhelp function.
#define EPS_E_LARGE_MODULE_NAME CM_EMAKEHR(0x120C)  //Moduel name > MAX_PATH
#define EPS_E_BAD_NETCF_PID CM_EMAKEHR(0x120D)  //NetCF Process isn't in the registry
#define EPS_E_RPC_BUFFER_LENGTH CM_EMAKEHR(0x120E)  //RPC Return buffer wasn't long enough
#define EPS_E_BAD_CONMAN_INSTALL CM_EMAKEHR(0x120F)  //Bad Instal - Reg key for install path was off.
#define EPS_E_BAD_NETCF_VERSION CM_EMAKEHR(0x1210)  //NETCF Version String is bad.
#define EPS_E_NETCF_EVENT_EXISTS CM_EMAKEHR(0x1211)  //NETCF Event Already exists - unexpected undesireable
#define EPS_E_NETCF_LAUNCH_THREAD CM_EMAKEHR(0x1212)  //NETCF thread suspend is off on launch- unexpected undesireable
#define EPS_E_NETCF_LAUNCH_PROCESS CM_EMAKEHR(0x1213)  //NETCF process exited before debugging could start - unexpected undesireable
#define EPS_E_INSUFFICIENT_CONTEXT_BUFFER CM_EMAKEHR(0x1214)  //Buffer to Get/SetThreadContext was too small.
#define EPS_E_BREAKPOINT_VALIDATION CM_EMAKEHR(0x1215)  //A breakpoint write failed verification
#define EPS_E_BREAKPOINT_REMOVAL CM_EMAKEHR(0x1216)  //Unexpected - a breakpoint can not be removed.
#define EPS_E_DATASTORE CM_EMAKEHR(0x1217)  //Failed to get access to the datastore.
#define EPS_E_NETCF_IPADDRESS CM_EMAKEHR(0x1218)  //Failed to get the device IP address.
#define EPS_E_NETCF_ENDPOINT_INFO CM_EMAKEHR(0x1219)  //Failed to get the device IP address.
#define EPS_E_PROCESS_NOT_FOUND CM_EMAKEHR(0x121A)  //Failed to find the process in the process list
#define EPS_E_BREAKPOINT_TYPE_UNKNOWN CM_EMAKEHR(0x121B)  //Breakpoint not understood.
#define EPS_E_BREAKPOINT_MALFORMED CM_EMAKEHR(0x121C)  //Breakpoint not properly formed - info inconsistent.
#define EPS_E_BREAKPOINT_DUPLICATE CM_EMAKEHR(0x121D)  //Breakpoint duplicate
#define EPS_E_NO_SITE CM_EMAKEHR(0x121E)  //Site not set
#define EPS_E_DEVICE_BAD_SPECIAL_PATH CM_EMAKEHR(0x121F)  //Couldn't process special path
#define EPS_E_DEACTIVATE_BP_VERIFY_WRITE CM_EMAKEHR(0x1220)  //Deactivating breakpoint - failed to verify write back
#define EPS_E_DEACTIVATE_BP_NOT_THERE CM_EMAKEHR(0x1221)  //Deactivating breakpoint - there was no BP there.
#define EPS_E_DEACTIVATE_BP_WRITE CM_EMAKEHR(0x1222)  //Deactivating breakpoint - failed to write back
#define EPS_E_PROCESS_DETACHED CM_EMAKEHR(0x1223)  //Process detached.
#define EPS_E_BAD_SESSION_GUID CM_EMAKEHR(0x1224)  //Unknown Session Guid - Call BeginDebugSession first.
#define EPS_E_TOOLHELP_UNAVAILABLE CM_EMAKEHR(0x1225)  //Unable to load toolhelp.dll
#define EPS_E_OS_DETACH_UNAVAILABLE CM_EMAKEHR(0x1226)  //Attempt to OS detach on an unsupported platform.
#define EPS_E_FAILED_TO_GET_PDATA CM_EMAKEHR(0x1227)  //failed to get PData
#define EPS_E_MEMORY_PAGE_MATH CM_EMAKEHR(0x1228)  //Unexpected error computing memory page information
#define EPS_E_BREAKPOINT_BPSIZE CM_EMAKEHR(0x1229)  //Wrong BP Size.
#define EPS_E_CEWATSON_PORT CM_EMAKEHR(0x122A)  //Attempting to consume port functionality from "cewatson" port - unsupported.
#define EPS_E_UNRECOGNIZED_PORT CM_EMAKEHR(0x122B)  //Unrecognized port.
#define EPS_E_INVALIDDATA CM_EMAKEHR(0x122C)   // External data invalid.
#define EPS_E_TOOMANYPROCESSES CM_EMAKEHR(0x122D)  // Too many processes returned.
#define EPS_S_DUPLICATE_SYNC_BP CM_SMAKEHR(0x122E) // Writing a duplicate sync breakpoint
#define EPS_S_SH4_OR_MIPS_NOP CM_SMAKEHR(0x122F) // Writing a breakpoint on a nop slot on Sh4 or Mips
#define EPS_E_LAST            CM_EMAKEHR(0x12FF)

#define EDBGCL_E_FIRST                      CM_EMAKEHR(0x3000)

#define EDBGCL_E_CONNECTIONBROKEN           CM_EMAKEHR(0x3001)
#define EDBGCL_E_CONNECTTRANSACTION         CM_EMAKEHR(0x3002)
#define EDBGCL_E_WRITE                      CM_EMAKEHR(0x3003)

#define EDBGCL_E_LOADINGCONMAN              CM_EMAKEHR(0x3050)
#define EDBGCL_E_SHOSMAPNOTFOUND            CM_EMAKEHR(0x3051)
#define EDBGCL_E_SHCPUMAPNOTFOUND           CM_EMAKEHR(0x3052)
#define EDBGCL_E_DATAERROR                  CM_EMAKEHR(0x3053)
#define EDBGCL_E_GETINSTRUCTIONSET          CM_EMAKEHR(0x3054)
#define EDBGCL_E_DATAMISSINGFROMDS          CM_EMAKEHR(0x3055)

#define EDBGCL_E_LAST                       CM_EMAKEHR(0x30FF)

#define EDMEXE_E_FIRST                      CM_EMAKEHR(0x3100)

#define EDMEXE_E_INVALIDFUNCTION            CM_EMAKEHR(0x3101)

#define EDMEXE_E_LAST                       CM_EMAKEHR(0x31FF)

//**** RPC System
#define RPC_E_FIRST                         CM_EMAKEHR(0x4400)
#define RPC_E_SURROGATE_FAILED_DLL_LOAD     CM_EMAKEHR(0x4401)     // The surrogate failed to load the DLL requested in the RPC call
#define RPC_E_SURROGATE_FAILED_FUNC_LOAD    CM_EMAKEHR(0x4402)     // The surrogate loaded the reqested DLL, but failed to load the requested function in the DLL
#define RPC_E_SURROGATE_BAD_FUNCNAME        CM_EMAKEHR(0x4403)     // Function name passed into surrogate has invalid characters preventing attempt to call GetProcAddress
#define RPC_E_SURROGATE_RPCDESCRIPTOR_INIT  CM_EMAKEHR(0x4404)     // The surrogate successfully created but failed to init an RPC descriptor when getting ready to execute RPC call; possible bogus data passed in from desktop side
#define RPC_E_SURROGATE_NULL_RPCDESCRIPTOR  CM_EMAKEHR(0x4405)     // The surrogate could not create instance of RPCDescriptor (out of memory?)
#define RPC_E_TM_READ_FAIL                  CM_EMAKEHR(0x4406)     // The surrogate had a problem calling TransactionManager->ReadObsolete()
#define RPC_E_RPCDLL_ZERO_LENGTH_FUNCNAME   CM_EMAKEHR(0x4407)     // The user asked RPC_DLL to call a function whose name has length 0
#define RPC_E_RPCDLL_ZERO_LENGTH_DLLNAME    CM_EMAKEHR(0x4408)     // The user asked RPC_DLL to call a function in a dll with name of length 0
#define RPC_E_RPCDLL_NULL_RPCDESCRIPTOR     CM_EMAKEHR(0x4409)     // The RPC_DLL could not create instance of RPCDescriptor (out of memory?)
#define RPC_E_RPCDLL_COULDNOTCONNECT        CM_EMAKEHR(0x440A)     // The RPC_DLL could not connect to the surrogate - Reason unknown
#define RPC_E_RPCDLL_MALFORMEDREQUEST       CM_EMAKEHR(0x440B)     // The RPC_DLL tried to create request bytes for transmit, but they were NULL or of length 0
#define RPC_E_RPCDLL_CONNECTTRANSACTION     CM_EMAKEHR(0x440C)     // The RPC_DLL tried to do CreateTransaction to transmit an RPC request, but CreateTransaction failed
#define RPC_E_RPCDLL_TMWRITE_FAIL           CM_EMAKEHR(0x440D)     // The RPC_DLL failed when trying to do a TransactionManager->Write() call
#define RPC_E_RPCDLL_DISCONNECTED           CM_EMAKEHR(0x440E)
#define RPC_E_SST_ADDDLLFORENTRY_FAIL       CM_EMAKEHR(0x440F)     // The SurrogateStatusTracker failed in AddDLLForEntry() - No empty slots?
#define RPC_E_SST_GETHINST_BADHINSTANCE     CM_EMAKEHR(0x4410)     // SurrogateStatusTracker failed in GetDLLHInstance because no matching HINSTANCE found
#define RPC_E_SST_GETHINST_BADDLLNAME       CM_EMAKEHR(0x4411)     // SurrogateStatusTracker failed in GetDLLHinstance because no matching DLLName found
#define RPC_E_CCT_COULDNOTCONNECT           CM_EMAKEHR(0x4412)     // The ClientConnectionTracker could not connect to surrogate, even though RPC bits were re-deployed successfully
#define RPC_E_CCT_DEPLOYRPC_FAIL            CM_EMAKEHR(0x4413)     // The ClientConnectionTracker tried to deploy RPC bits and failed.
#define RPC_E_CCT_RESOLVEDEPLOYPATH_FAIL    CM_EMAKEHR(0x4414)     // The ClientConnectionTracker could not figure out the path to the RPC bits for deployment
#define RPC_E_CCT_DEPLOYRPC_CCINSTANCE      CM_EMAKEHR(0x4415)     // The ClientConnectionTracker could not do CoCreateInstance to get IID_IConmanServer
#define RPC_E_CCT_DEPLOYRPC_CINITEX         CM_EMAKEHR(0x4416)     // The ClientConnectionTracker in DeployRPCBits failed to do CoInitializeEx
#define RPC_E_CCT_BADDLLNAME                CM_EMAKEHR(0x4417)     // No surrogate connections are registered in the ClientConnectionTracker for the DLL name passed as an argument
#define RPC_E_RPCCALL_NULLINARGS            CM_EMAKEHR(0x4418)     // The inArgs argument to RPC_Call may not be NULL if the inArgLength argument is greater than 0
#define RPC_E_CCT_PATHTOTRANSPORT_TOOLONG   CM_EMAKEHR(0x4419)     // The path to the transports, as stored in the registry and used by ClientConnectionTracker, is too long
#define RPC_E_CCT_REGISTRY_ERROR            CM_EMAKEHR(0x441a)     // ClientConnectionTracker had an unexpected failure when accessing the registry.
#define RPC_E_RPCSVR_START_SURROGATE_FAIL   CM_EMAKEHR(0x441B)     // The RPC Server thread failed to launch the surrogate; ConmanSurrogate.exe is probably not on device
#define RPC_E_NOT_YET_IMPL                  CM_EMAKEHR(0x441C)     // Things that are not yet implemented can return this value
#define RPC_E_RPCSERVER_STARTING            CM_EMAKEHR(0x441D)     // RPC Server cannot quit because it is activly starting
#define RPC_E_RPCSERVER_SHOULDQUIT          CM_EMAKEHR(0x441E)     // RPC Server knows it should quit, but has not yet made it into the HasQuit state 
#define RPC_E_RPCSERVER_NO_PORT_FOR_DLL     CM_EMAKEHR(0x441F)     // Given a DLL name, no surrogate was found listening on a port that can field RPC calls into that DLL
#define RPC_E_RPCSVR_DETECT_SURROGATE_FAIL  CM_EMAKEHR(0x4420)     // Although the OS says it launched a surrogate, its presence cannot be detected by the RPCServer
#define RPC_E_PORT_OUTOFRANGE               CM_EMAKEHR(0x4421)     // The RPC System was asked to use a port that is beyond the legal range
#define RPC_E_SSE_DLLSLOT_NOTFOUND          CM_EMAKEHR(0x4422)     // The SurrogateStatusEntry failed to find the requested DLL SLOT
#define RPC_E_SSE_EMPTYSLOT_NOTFOUND        CM_EMAKEHR(0x4423)     // The SurrogateStatusEntry failed to find an empty DLL SLOT
#define RPC_E_SST_INVARIANT                 CM_EMAKEHR(0x4424)     // SurrogateStatusTracker failed invariant check
#define RPC_E_SST_NOMOREENTRIES             CM_EMAKEHR(0x4425)     // No more empty entries are left in the surrogatestatustracker - too many surrogates running?
#define RPC_E_SST_CANNOT_GET_MAPVIEW        CM_EMAKEHR(0x4426)     // The SurrogateStatusTracker was unable to get a handle to to the shared memory file map view
#define RPC_E_SST_CANNOT_GET_SHAREDMEM      CM_EMAKEHR(0x4427)     // Attempts to get access to the shared memory area failed for reasons unknown
#define RPC_E_SVRREQHANDLER_INVARIANT       CM_EMAKEHR(0x4428)     // RPCServerREquestHandler failed its invariant check
#define RPC_E_CCT_INVARIANT                 CM_EMAKEHR(0x4429)     // ClientConnectionTracker failed its invariant check
#define E_RPC_CCT_TM_CACHE_FULL             CM_EMAKEHR(0x442a)     // ClientCOnnectionTracker has no more slots available for cached TM connections
#define E_RPC_SURROGATE_CREATETHREADS       CM_EMAKEHR(0x442B)     // Surrogate could not create threads for the thread pool
#define E_RPC_NOEMPTYGUIDSLOT               CM_EMAKEHR(0x442C)     // Surrogate could not find a free slot for a guid representing an RPC call
#define E_RPC_SURROGATECOMMMGR_INVARIANT    CM_EMAKEHR(0x442D)     // SurrogateCommunicationManager flunked its invariant check
#define E_RPC_SURROGATECOMMMGR_INIT         CM_EMAKEHR(0X442E)     // SurrogateComminicationManager failed to Init propertly (insuffecient resources?)
#define E_RPC_CCT_DATASTORE_ACCESS          CM_EMAKEHR(0X442F)     // ClientConnectionTracker had a problem maniuplating the DataStore
#define E_RPC_CCT_DATASTORE_CONTENTS        CM_EMAKEHR(0X4430)     // ClientConnectionTracker reports an invalid value in a DataStore entry
#define E_RPC_CANNOT_LAUNCH_SURROGATE       CM_EMAKEHR(0X4431)     // The RPC server cannot launch a surrogate
#define E_GUIDKEYEDLIST_EMPTY               CM_EMAKEHR(0x4432)     // The GUIDKeyedList is empty, but user tried to access the first node
#define E_GUIDKEYEDLIST_NODENOTFOUND        CM_EMAKEHR(0x4433)     // Tried to find a GUID in a GuidKeyedList that was not found
#define E_TMMANAGER_THREADCREATION          CM_EMAKEHR(0x4434)     // TMManager was doing AcceptTMConnection but was unable to launch its reader thread
#define E_TMMANAGER_INVALIDSTATE            CM_EMAKEHR(0x4435)     // Caller tried to call a method on a TMManager when it was in the wrong state, eg, do ConnectTransaction when not in Initialized_Connected
#define E_GUIDKEYEDLIST_INVARIANT           CM_EMAKEHR(0x4436)     // Guid Keyed List object failed its invariant check
#define E_TMMANAGER_PACKETOUTOFSYNC               CM_EMAKEHR(0x4437)       // TMManager detects packet out-of-sync error in the underlying transport layer
#define E_PACKETLISTNODE_EMPTY              CM_EMAKEHR(0x4438)     // Tried to get the packet data from an empty PacketListNode that has no data in its buffer
#define E_NOSURROGATECONNECTIONFOUND        CM_EMAKEHR(0x4439)     // The ClientConnectionTracker can't find a cached connection for a surrogate (could mean need to launch surrogate / is expected error in that case)
#define E_RPC_TARGET_DLL_MISSING            CM_EMAKEHR(0x443a)     // RPC server cannot launch a surrogate because the corresponding target DLL is missing
#define E_RPC_DEVICE_RPC_BINS_MISSING       CM_EMAKEHR(0x443B)     // There are device side RPC binaries missing
#define RPC_E_SURROGATE_FAILED_TL_LOAD      CM_EMAKEHR(0x443C)     // Could not load the transport loader.
#define RPC_E_TL_GETTRANSPORT               CM_EMAKEHR(0x443D)     // Could not get the transport.
#define RPC_E_TL_ACKNOWLEDGE                CM_EMAKEHR(0x443E)     // Could not acknowledge launch with transport loader.
#define RPC_E_TL_REGISTERCALLBACK           CM_EMAKEHR(0x443f)     // Could not register callback.


#define RPC_E_LAST                          CM_EMAKEHR(0x44FF)

//**** Transports
#define TR_E_FIRST              _HRESULT_TYPEDEF_(0x89731300L)
#define TR_E_INTERRUPTED        _HRESULT_TYPEDEF_(0x89731300L)  // a blocking operation was interrupted or aborted
#define TR_E_DISCONNECTED       _HRESULT_TYPEDEF_(0x89731301L)  // the target transport was disconnected before doing any work inside the current call
#define TR_E_INVALID_CONFIG _HRESULT_TYPEDEF_(0x89731302L)  // the config passed in was invalid, inadequate for setting up the transport
#define TR_E_AUTOCONF_ADDR  _HRESULT_TYPEDEF_(0x89731303L)  // couldn't reach a device with an auto-configured address
#define TR_E_INVALID_IPADDRESS   _HRESULT_TYPEDEF_(0x89731304L)  
#define TR_E_LAST               _HRESULT_TYPEDEF_(0x897313FFL)

//**** Transaction Manager
#define TM_E_FIRST                      CM_EMAKEHR(0x1400)
#define TM_E_TRANSACTION_NOT_FOUND  CM_EMAKEHR(0x1401) // the specified transaction was not found
#define TM_E_TRANSACTION_CLOSED     CM_EMAKEHR(0x1402) // the peer closed the transaction
#define TM_E_LAST                       CM_EMAKEHR(0x14FF)

//**** ConManServer
#define SRV_E_FIRST                         CM_EMAKEHR(0x1500)
#define SRV_E_NOT_INIT                      _HRESULT_TYPEDEF_(0x89731501L)  // calling a method on an uninitialized server
#define SRV_E_BOOTSTRAP_INIT_FAILED         CM_EMAKEHR(0x1502) // the bootstrap could not be initialized
#define SRV_E_TRANSACTION_NOT_FOUND         CM_EMAKEHR(0x1503) // the specified transaction was not found
#define SRV_E_UNSUP_PACKET_MSG              CM_EMAKEHR(0X1504) // unsupported packet header message
#define SRV_E_FILE_OP_CANCELED              CM_EMAKEHR(0X1505) // the currently running file operation (send/receive) was canceled
#define SRV_E_NOT_CONNECTED                 CM_EMAKEHR(0x1506) // the server is not currently connected
#define SRV_E_NO_PREV_SNAPSHOT              CM_EMAKEHR(0x1507) // a call to EnumProcesses failed because requesting old snapshot when not called before with new snapshot
#define E_SRV_MANGLED_PACKET                CM_EMAKEHR(0x1508) // something is wrong with the packet that the server is processing
#define SRV_E_SHUTDOWN_FAILED               _HRESULT_TYPEDEF_(0x89731509L) 
#define SRV_E_GRACEFUL_SHUTDOWN_FAILED      _HRESULT_TYPEDEF_(0x8973150AL) 
#define SRV_E_FORCEFUL_SHUTDOWN_FAILED      _HRESULT_TYPEDEF_(0x8973150BL) 
#define SRV_E_INCOMPATIBLE_CLIENT           CM_EMAKEHR(0x150C) // the client on the device is incompatible with this server
#define SRV_E_CPU_NOT_SUPPORTED             CM_EMAKEHR(0x150D) // the cpu type/architecture/instructionset was not compatible with platform
#define E_STARTUP_COULDNOTWRITEPUBKEYINFO   CM_EMAKEHR(0x150E) // The startup could not write public key information in the PREP phase
#define SRV_E_REBOOT_FILES_NOT_SAME         _HRESULT_TYPEDEF_(0x8973150FL) 
#define SRV_E_NEWER_DEVICE_WINDOWS_NEEDED   _HRESULT_TYPEDEF_(0x89731510L) 
#define SRV_E_CMACCEPT_FAILED               _HRESULT_TYPEDEF_(0x89731511L) 
#define SRV_E_CMACCEPT_FILE_NOT_FOUND       _HRESULT_TYPEDEF_(0x89731512L) 
#define SRV_E_INVALID_SIGNATURE             _HRESULT_TYPEDEF_(0x89731513L)
#define SRV_E_LAST                          CM_EMAKEHR(0x15FF)

//**** ConManClient errors
#define CLI_E_FIRST                         _HRESULT_TYPEDEF_(0x89731600L)
#define CLI_E_WONT_OVERWRITE                _HRESULT_TYPEDEF_(0x89731600L)  // the file being sent from the server can't be overwritten because the file exists and FileAction_Create was specified
#define CLI_E_NO_CURRENT_PROCESS_RUNNING        _HRESULT_TYPEDEF_(0x89731601L)  // GetProcessExitCode identified no currently running process.
#define CLI_E_UNEXPECTED_FILE_DATA          _HRESULT_TYPEDEF_(0x89731602L)  // Unexpected file data block without initiating SendFile properly.
#define CLI_E_UNSUP_PACKET_MSG              _HRESULT_TYPEDEF_(0x89731603L)  // unsupported packet header message
#define CLI_E_UNKNOWN_THREAD_TERMINATION_CONDITION  _HRESULT_TYPEDEF_(0x89731604L)  // unknown thread terminated condition during shutdown.  
#define CLI_E_PACKET_REGISTRATION           _HRESULT_TYPEDEF_(0x89731605L)  // error registering packet type handler.
#define CLI_E_HANDSHAKE                 _HRESULT_TYPEDEF_(0x89731606L)  // handshake error
#define CLI_E_NO_PROCESS_EXIT_INFO_AVAILABLE        _HRESULT_TYPEDEF_(0x8973160CL)  // no process exit information available
#define E_CLI_NO_IPADDRESS                  _HRESULT_TYPEDEF_(0x8973160DL)  // The ConmanClient can't figure out its IP address - this is a bad and very unexpected failure

// ****ConManClient Success Status codes
#define CLI_E_HANDSHAKE_FAILURE                             CM_SMAKEHR(0x1600) // handshake protocol failure
#define CLI_E_HANDSHAKE_PROTOCOL_RESPOND_UNKNOWN                    CM_SMAKEHR(0x1601) // handshake protocol respond unknown
#define CLI_E_HANDSHAKE_PROTOCOL_ACCEPT_VERSION                     CM_SMAKEHR(0x1602) // handshake protocol accept version
#define CLI_E_HANDSHAKE_PROTOCOL_REJECT_VERSION                     CM_SMAKEHR(0x1603) // handshake protocol reject version
#define CLI_E_HANDSHAKE_PROTOCOL_PROPOSE_VERSION                    CM_SMAKEHR(0x1604) // handshake protocol propose version
#define E_CONMANCLIENT_PREP_READDESKTOPFILE         CM_EMAKEHR(0x1605) // Client could not read the prep info from the desktop during the prep stage
#define E_CLIENT_PREP_WRITE_SESSIONKEY              CM_EMAKEHR(0x1607) // Client could not write its session key information to a file
#define E_CLIENT_PREP_OPEN_LOCALKEYFILE             CM_EMAKEHR(0x1608) // Client could not open the file storing the local session key
#define E_CLIENT_PREP_OPEN_DESKTOPFILE              CM_EMAKEHR(0x1609) // Client could not open the file that holds the desktop's public key and other info.
#define CLI_E_LAST                                      CM_EMAKEHR(0x16FF)

//**** dip
#define DIP_E_FIRST                         _HRESULT_TYPEDEF_(0x89731700L)  // CM_EMAKEHR(0x1700)
#define DIP_E_PACKAGENOTFOUND               _HRESULT_TYPEDEF_(0x89731700L) // package not found
#define DIP_E_INVALIDDEPENDENTREFERENCES    _HRESULT_TYPEDEF_(0x89731701L) // Invalid dependent references
#define DIP_E_INVALIDREFERENCETYPEFLAGS     _HRESULT_TYPEDEF_(0x89731702L) // Invalid reference flags.
#define DIP_E_NOVALIDLCIDMATCH              _HRESULT_TYPEDEF_(0x89731703L)
#define DIP_E_INSTRUCTION_SET_NOT_MATCHED   _HRESULT_TYPEDEF_(0x89731704L)
#define DIP_E_POST_DEPLOY_ERROR             _HRESULT_TYPEDEF_(0x89731705L)
#define DIP_E_SENDFILE_FAIL                 _HRESULT_TYPEDEF_(0x89731706L)
#define DIP_E_LAST                          _HRESULT_TYPEDEF_(0x89731750L)  // CM_EMAKEHR(0x1750)

//**** Bootstraps
#define BOOT_E_FIRST                    _HRESULT_TYPEDEF_(0x89731800L) 
#define BOOT_E_PORT_IN_USE              _HRESULT_TYPEDEF_(0x89731800L) // a specified communication resource (port) is already in use by another application
#define BOOT_E_PROXY_PORTS_MISSING  _HRESULT_TYPEDEF_(0x89731801L) // the required ActiveSync proxy ports are not registered
#define BOOT_E_INVALID_CONFIG           _HRESULT_TYPEDEF_(0x89731802L) // the config passed in was invalid, inadequate for setting up the bootstrap
#define BOOT_E_RAPI_NOT_INSTALLED       _HRESULT_TYPEDEF_(0x89731803L)  // ActiveSync is not installed
#define BOOT_E_ACTIVESYNC_VERSION       _HRESULT_TYPEDEF_(0x89731804L)  // The curren version of ActiveSync is not supported, please go to www.microsoft.com ... and install the latest version
#define BOOT_E_BOOTSTRAP_LOAD_FAILURE       _HRESULT_TYPEDEF_(0x89731805L)  
#define BOOT_E_BOOTSTRAP_COPY_FAILURE       _HRESULT_TYPEDEF_(0x89731806L)  
#define BOOT_E_BOOTSTRAP_INITPATH_FAILURE       _HRESULT_TYPEDEF_(0x89731807L)  
#define BOOT_E_BOOTSTRAP_INIT_FAILURE       _HRESULT_TYPEDEF_(0x89731808L)  
#define BOOT_E_BOOTSTRAP_CONFIG_FAILURE     _HRESULT_TYPEDEF_(0x89731809L)  
#define BOOT_E_BOOTSTRAP_SHUTDOWN_FAILURE       _HRESULT_TYPEDEF_(0x8973180AL)
#define BOOT_E_BOOTSTRAP_DEVICEEMULATORNOTREGISTERED _HRESULT_TYPEDEF_(0x8973180BL)
#define BOOT_E_LAST                     _HRESULT_TYPEDEF_(0x897318FFL) 

//**** Host Launcher/Transport Loader
#define HL_E_FIRST                      _HRESULT_TYPEDEF_(0x89731900L) 
#define HL_E_UNEXPECTED_FORMAT          _HRESULT_TYPEDEF_(0x89731900L) //Formatting error int the shared data source
#define HL_E_BAD_ENVIRONMENT            _HRESULT_TYPEDEF_(0x89731901L) //The environment parameter was corrupt.
#define HL_E_BAD_CREATE_PROC_FLAGS      _HRESULT_TYPEDEF_(0x89731902L) //The process create flags are not supported by HL/CE
#define HL_E_RESUME_THREAD              _HRESULT_TYPEDEF_(0x89731903L) //Unexpected error resuming a thread.
#define HL_E_HOST_EXIT                  _HRESULT_TYPEDEF_(0x89731904L) //Host exited before signaling event.
#define HL_E_NO_EVENT                   _HRESULT_TYPEDEF_(0x89731905L) //Syncronization event does not exist
#define HL_E_PORT_IN_USE                _HRESULT_TYPEDEF_(0x89731906L) //TCP port is in use.
#define HL_E_NO_PORT                    _HRESULT_TYPEDEF_(0x89731907L) //port range exhausted
#define HL_E_CREATE_STREAM_CANCELED     _HRESULT_TYPEDEF_(0x89731908L) //Create Stream Canceled
#define HL_E_BAD_TRANSPORT              _HRESULT_TYPEDEF_(0x89731909L) //Transport invalid
#define HL_E_BAD_SERVICE_ID             _HRESULT_TYPEDEF_(0x8973190AL) //Improper Service Id
#define HL_E_NO_SHUTDOWN_EVENT          _HRESULT_TYPEDEF_(0x8973190BL) //Shutdown event not present - terminating the process instead.
#define HL_E_HANDSHAKE                  _HRESULT_TYPEDEF_(0x8973190CL) //Handshake error - check versioning and API useage.
#define HL_E_SENDFILE_FAIL_DESKTOP      _HRESULT_TYPEDEF_(0x8973190DL) // "Error opening file %1!s! with error: %2!#0.8x! %3!s!.\n"
#define HL_E_SENDFILE_FAIL_DEVICE       _HRESULT_TYPEDEF_(0x8973190EL) // "Error writting file %1!s! with error: %2!#0.8x! %3!s!.\n"
#define HL_E_LAST                       _HRESULT_TYPEDEF_(0x897319FFL)

#define HL_E_SCENARIO_GENERIC_CONNECT_FAILURE _HRESULT_TYPEDEF_(0x89732000L)
#define HL_E_SCENARIO_GENERIC_DEPLOY_FAILURE _HRESULT_TYPEDEF_(0x89732001L)
#define HL_E_SCENARIO_TARGETUNREACHABLE    _HRESULT_TYPEDEF_(0x89732002L) // scenario failure: target unreachable..
#define HL_E_SCENARIO_TARGETUNREACHABLE_NONTCP _HRESULT_TYPEDEF_(0x89732003L) // scenario failure: target unreachable.. (non-TCP)
#define HL_E_SCENARIO_NOTARGETADDRESS      _HRESULT_TYPEDEF_(0x89732004L) // scenario failure: No target address detected..
#define HL_E_SCENARIO_DEVICENOTREADY       _HRESULT_TYPEDEF_(0x89732005L) // scenario failure: device not ready..
#define HL_E_SCENARIO_NOVIRTUALSWITCH   _HRESULT_TYPEDEF_(0x89732006L) // scenario failure: (possibly) no virtual switch.

//**** Native Jit Debugger
#define EDBGJIT_E_FIRST                 CM_EMAKEHR(0x1A00)
#define EDBGJIT_E_REBOOT_REQUIRED   CM_EMAKEHR(0x1A00)
#define EDBGJIT_E_LAST                  CM_EMAKEHR(0x1AFF)

//**** CMAccept
#define CMACCEPT_E_FIRST                    CM_EMAKEHR(0x1B00)
#define CMACCEPT_E_SHUTTING_DOWN    CM_EMAKEHR(0x1B00)
#define CMACCEPT_E_CLIENT_NOT_RUNNING   CM_EMAKEHR(0x1B01)
#define CMACCEPT_E_LAST                 CM_EMAKEHR(0x1BFF)

//**** Emulator COM Interface ...
#define EMUL_E_FIRST                    CM_EMAKEHR(0x1C00)
#define EMUL_E_LOAD_RESTOREIMAGE_FAILED             _HRESULT_TYPEDEF_(0x89731C00L)
#define EMUL_E_LAUNCH_PARAMETERS                        _HRESULT_TYPEDEF_(0x89731C01L)
#define EMUL_E_LAST                 CM_EMAKEHR(0x1CFF)

//**** Magneto Errors ...
#define E_SIGNATURE_NOT_ACCEPTED _HRESULT_TYPEDEF_(0x800775AB)  //HRESULT for Magneto indicating the process was not allowed to run due to security reasons (prompt or device settings).

//**** Next...

#endif // __CONMAN_HRESULTS__

