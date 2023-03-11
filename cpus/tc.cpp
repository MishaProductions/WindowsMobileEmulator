/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

Module Name:

    tc.cpp

Abstract:

    This module implements the Translation Cache, where guest code is
    translated into native code.
    

--*/

#include "emulator.h"
#include "entrypt.h"
#include "tc.h"
#include "CPU.h"

// Include the logging infrastructure
#include "dev_emulator_log.h"
#define TMH_FILENAME "tc.tmh"
#include "vsd_logging_inc.h"

// Tunable configuration parameters.
const DWORD CpuCacheGrowTicks=100;
const DWORD CpuCacheShrinkTicks=1000;
const size_t CpuCacheChunkMax=256*1024;
const size_t CpuCacheChunkMin=8*1024;
const DWORD CACHE_LINE_SIZE=32;

#ifdef DEBUG
#define DBG_FILL_VALUE  0xcc			// a bad instruction
#endif //DEBUG

//
// Descriptor for a range of the Translation Cache.
//
class ITranslationCache {
public:
    bool Initialize(void);
    unsigned __int8 *Allocate(size_t Size);
    void Flush(void);
    void FreeUnusedTranslationCache(unsigned __int8 *StartOfFree);
    __inline size_t MaxSize(void) { return m_MaxSize; };

private:
    unsigned __int8* m_StartAddress; // base address for the cache
    size_t m_MaxSize;         // max size of the cache (in bytes)
    size_t m_MinCommit;       // min amount that can be committed (bytes)
    size_t m_NextIndex;       // next free address in the cache
    size_t m_CommitIndex;     // next uncommitted address in the cache
    size_t m_ChunkSize;       // amount to commit by
    DWORD  m_LastCommitTime;  // time of last commit
    bool   m_DecommitOnFlush; // decommit the cache on flush 
};

class ITranslationCache CTranslationCache;


bool ITranslationCache::Initialize(void)
{
    //
    // Initialize non-zero fields in the CACHEINFO
    //
    // Read cache info from the environment
    DWORD cacheSize = 32;
    wchar_t EnvValue[5];
    DWORD dwRet = GetEnvironmentVariableW(L"DE_MAXCACHESIZE", EnvValue, 5);
    if ( dwRet != 0 && dwRet <= 4 && EnvValue[0] )
    {
        wchar_t *EndCharacter;
        cacheSize = wcstoul(EnvValue, &EndCharacter, 10);
        if (*EndCharacter != '\0') {
            // Found some garbage characters along with the count
            cacheSize = 32;
            LOG_ERROR(CACHE, "Failed to parse the environment var - DE_MAXCACHESIZE = %S", EnvValue);
        }
        LOG_INFO(CACHE, "Setting max cache to - %d MB", cacheSize);
    }
    m_DecommitOnFlush = true;
    dwRet = GetEnvironmentVariableW(L"DE_NORESIZEONFLUSH", EnvValue, 4);
    if ( dwRet != 0 && dwRet <= 3 && EnvValue[0] == '1' )
    {
        m_DecommitOnFlush = false;
        LOG_INFO(CACHE, "Turning off resize on flush");
    }

    m_MaxSize =     cacheSize*1024*1024;  // 32mb of reserve
    m_MinCommit =      256*1024;  // 256k of commit
    m_ChunkSize =       16*1024;  // commit in 16k chunks

    // Create the cache reserve
    m_StartAddress = (unsigned __int8 *)VirtualAlloc(NULL, m_MaxSize, MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!m_StartAddress) {
        return false;
    }

    //
    // Make the initial commit
    //
    if (!VirtualAlloc(m_StartAddress, m_MinCommit, MEM_COMMIT, PAGE_EXECUTE_READWRITE)) {
        return false;
    }
    m_LastCommitTime = GetTickCount();

#ifdef DEBUG
    //
    // Fill the TC with a unique illegal value, so we can distinguish
    // old code from new code and detect overwrites.
    //
    memset(m_StartAddress, DBG_FILL_VALUE, m_MinCommit);
#endif //DEBUG

    return true;
}



/*++

Routine Description:

    Allocate space within a Translation Cache.  If there is insufficient
    space, the allocation will fail.

Arguments:

    Cache           - Data about the cache
    Size            - Size of the allocation request, in bytes
    
Return Value:

    Pointer to DWORD-aligned memory of 'Size' bytes.  NULL if insufficient
    space.

--*/
unsigned __int8 *ITranslationCache::Allocate(size_t Size)
{
    unsigned __int8 *Address;

    // Ensure parameters and cache state are acceptable
    ASSERT(m_NextIndex == 0 || m_StartAddress[m_NextIndex-1] != DBG_FILL_VALUE); //  Cache Corrupted
    ASSERT(m_NextIndex == m_CommitIndex || m_StartAddress[m_NextIndex] == DBG_FILL_VALUE);

    #pragma prefast(suppress:22008, "Prefast doesn't understand that the check below is on m_NextIndex as well")  
    if ((m_NextIndex + Size) >= m_MaxSize || (m_NextIndex + Size) < m_NextIndex) {
        //
        // Not enough space in the cache.
        //
        LOG_VERBOSE(CACHE, "Cache is failing to allocate - %x bytes. m_NextIndex - %x ", Size, m_NextIndex);
        return NULL;
    }

    Address = &m_StartAddress[m_NextIndex];
    m_NextIndex += Size;

    if (m_NextIndex > m_CommitIndex) {
        //
        // Need to commit more of the cache
        //
        size_t RegionSize;
        DWORD CommitTime = GetTickCount();

        if ((CommitTime-m_LastCommitTime) < CpuCacheGrowTicks) {
            //
            // Commits are happening too frequently.  Bump up the size of
            // each commit.
            //
            if (m_ChunkSize < CpuCacheChunkMax) {
                m_ChunkSize *= 2;
            }
        } else if ((CommitTime-m_LastCommitTime) > CpuCacheShrinkTicks) {
            //
            // Commits are happening too slowly.  Reduce the size of each
            // Commit.
            //
            if (m_ChunkSize > CpuCacheChunkMin) {
                m_ChunkSize /= 2;
            }
        }

        RegionSize = m_ChunkSize;
        if (RegionSize < Size) {
            //
            // The commit size is smaller than the requested allocation.
            // Commit enough to satisfy the allocation plus one more like it.
            //
            RegionSize = Size*2;
        }
        if (RegionSize+m_CommitIndex >= m_MaxSize) {
            //
            // The ChunkSize is larger than the remaining free space in the
            // cache.  Use whatever space is left.
            //
            RegionSize = m_MaxSize - m_CommitIndex;
        }
        RegionSize = (RegionSize+4095) & ~4095;  // round up to nearest page size

        LOG_VERBOSE(CACHE, "Commiting more cache space - %x. Commit Index %x ", RegionSize, m_CommitIndex);
        if (!VirtualAlloc(&m_StartAddress[m_CommitIndex], RegionSize, MEM_COMMIT, PAGE_EXECUTE_READWRITE)) {
            //
            // Commit failed.  Caller may flush the caches in order to
            // force success (as the static cache has no commit).
            //
            LOG_ERROR(CACHE, "Commit failed - returning NULL");
            return NULL;
        }

#ifdef DEBUG
        //
        // Fill the TC with a unique illegal value, so we can distinguish
        // old code from new code and detect overwrites.
        //
        memset(&m_StartAddress[m_CommitIndex], DBG_FILL_VALUE, RegionSize);
#endif //DEBUG
        m_CommitIndex += RegionSize;
        m_LastCommitTime = CommitTime;

    }

    return Address;
}


/*++

Routine Description:

    Flush out a Translation Cache.

Arguments:

    None
    
Return Value:

    .

--*/
void ITranslationCache::Flush(void)
{
    //
    // Only decommit pages if the current commit size is >= the size
    // we want to shrink to.  It may not be that big if somebody called
    // CpuFlushInstructionCache() before the commit got too big.
    //
    LOG_VVERBOSE(CACHE, "Cache flush called");
    if (m_CommitIndex > m_MinCommit && m_DecommitOnFlush ) {
        m_LastCommitTime = GetTickCount();
		
        #pragma prefast(suppress:250, "We do not want to free address descriptors")
        if (!VirtualFree(&m_StartAddress[m_MinCommit], m_CommitIndex - m_MinCommit, MEM_DECOMMIT)) {
            ASSERT(FALSE);
        }
        LOG_VERBOSE(CACHE, "Cache flush is decreasing cache size from %x to %x", m_CommitIndex, m_MinCommit);
        m_CommitIndex = m_MinCommit;
    }

#ifdef DEBUG
    //
    // Fill the Cache with a unique illegal value, so we can
    // distinguish old code from new code and detect overwrites.
    //
    memset(m_StartAddress, DBG_FILL_VALUE, m_CommitIndex);
#endif //DEBUG

    m_NextIndex = 0;
}

void
ITranslationCache::FreeUnusedTranslationCache(
    unsigned __int8 *StartOfFree
    )
/*++

Routine Description:

    After allocating from the TranlsationCache, a caller can free the tail-
    end of the last allocation.

Arguments:

    StartOfFree -- address of first unused byte in the last allocation
    
Return Value:

    .

--*/
{
    unsigned __int8* AlignedStartOfFree;

    ASSERT(StartOfFree > (unsigned __int8 *)m_StartAddress &&
           StartOfFree <= (unsigned __int8 *)m_StartAddress + m_NextIndex);

    // DWORD-align the pointer
    AlignedStartOfFree = (unsigned __int8*)(((size_t)StartOfFree+3) & ~3);
    if (AlignedStartOfFree != StartOfFree) {
        AlignedStartOfFree[-1] = 0x90; // avoid cache-corruption assertions
    }

    m_NextIndex = AlignedStartOfFree - m_StartAddress;
}

unsigned __int8 *
AllocateTranslationCache(
    size_t Size
    )
/*++

Routine Description:

    Allocate space within the Translation Cache.  If there is insufficient
    space, the cache will be flushed.  Allocations are guaranteed to
    succeed.

Arguments:

    Size            - Size of the allocation request, in bytes
    
Return Value:

    Pointer to memory of 'Size' bytes.  Always non-NULL.

--*/
{
    unsigned __int8 *Address;

    //
    // Check parameters
    //
    ASSERT(Size <= CTranslationCache.MaxSize());

    //
    // Try to allocate from the cache
    //
    Address = CTranslationCache.Allocate(Size);
    if (!Address) {
        //
        // Translation cache is full - time to flush Translation Cache
        // (Both Dyn and Stat caches go at once).
        //
        LOG_INFO(CACHE, "Cache is full - calling flush");
        FlushTranslationCache(0, 0xffffffff);
    }

    return Address;
}

void
FreeUnusedTranslationCache(
    unsigned __int8 *StartOfFree
    )
/*++

Routine Description:

    After allocating from the TranlsationCache, a caller can free the tail-
    end of the last allocation.

Arguments:

    StartOfFree -- address of first unused byte in the last allocation
    
Return Value:

    .

--*/
{
    CTranslationCache.FreeUnusedTranslationCache(StartOfFree);
}



void
FlushTranslationCache(
    unsigned __int32 GuestAddr,
    unsigned __int32 GuestLength
    )
/*++

Routine Description:

    Indicates that a range of guest memory has changed and that any
    native code in the cache which corresponds to that guest memory is stale
    and needs to be flushed.

    GuestAddr = 0, GuestLength = 0xffffffff guarantees the entire cache is
    flushed.

Arguments:

    GuestAddr   -- Intel address of the start of the range to flush
    GuestLength -- Length (in bytes) of memory to flush
    
Return Value:

    .

--*/
{
    LOG_VVERBOSE(CACHE, "Flush is called from address %x length %x", GuestAddr, GuestLength);
    // We have to flush at least a cache line
    if (GuestLength != 0xffffffff ) {
        GuestAddr = GuestAddr & ~(CACHE_LINE_SIZE-1);
        GuestLength = ( GuestLength + CACHE_LINE_SIZE - 1 +
                        (GuestAddr & (CACHE_LINE_SIZE-1))) & ~(CACHE_LINE_SIZE - 1);
    }
    if (GuestLength == 0xffffffff ||
        IsGuestRangeInCache(GuestAddr, GuestLength)) {

        CpuFlushCachedTranslations();
        FlushEntrypoints();
        CTranslationCache.Flush();
    }
}



bool
InitializeTranslationCache(
    void
    )
/*++

Routine Description:

    Per-process initialization for the Translation Cache.

Arguments:

    .
    
Return Value:

    .

--*/
{
    return CTranslationCache.Initialize();
}

