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
#include "MappedIO.h"
#include "COMInterface.h"
#include "Devices.h"
#include "PCMCIADevices.h"
#include "Board.h"
#include "resource.h"
#include "CPU.h"
#include "wincevkkeys.h"
#include <conio.h>            // for _kbhit() and getch()

// Include the logging infrastructure
#include "dev_emulator_log.h"
#define TMH_FILENAME "devices.tmh"
#include "vsd_logging_inc.h"

#define MAPPEDIODEVICE(baseclass, devicename, iobase, iolength) \
    class baseclass devicename;
#include "mappediodevices.h"
#undef MAPPEDIODEVICE

#define VK_MATRIX_FN   0xC0

struct VirtualKeyMapping {
    unsigned __int32 uiVk;
    unsigned __int32 uiVkGenerated;
};

extern "C" {
extern const unsigned __int8 VKtoSCTable[256];      // defined in VKtoScan.c
extern const VirtualKeyMapping g_rgvkMapFn[21];     // defined in VKtoScan.c
extern const VirtualKeyMapping g_rgvkMapNumLock[15];// defined in VKtoScan.c
}

void __fastcall BoardWriteGuestArguments(WORD Width, WORD Height, WORD BitsPerPixel, bool SoftReset);

extern unsigned __int32 FlashBank0[];

// Instantiate PCMCIA devices here.
IONE2000 NE2000;
// End of PCMCIA devices.


#define TO_BCD(n)    (((n)/10)<<4) | ((n)%10)
#define FROM_BCD(n)  ((WORD)( ((n)>>4)*10 + (n & 0xf) ))
#define YEAR_ADJUST 2000

bool __fastcall IOIIS::PowerOn()
{
    return InitAudio();
}

bool IOIIS::InitAudio()
{
    // Create a thread for receiving messages from the wave mapper
    HANDLE hAudioLoopThread;
    hAudioLoopThread=CreateThread(NULL, 0, IOIIS::AudioIOLoopStatic, this, 0, &m_dwAudioThreadId);
    if (hAudioLoopThread == INVALID_HANDLE_VALUE) {
        return false;
    }

    // The driver only configures the audio to one particular configuration
    // so it can be kept statically
    WAVEFORMATEX outStreamState;
    outStreamState.wFormatTag = WAVE_FORMAT_PCM;
    outStreamState.nChannels = 2;
    outStreamState.nSamplesPerSec = 44100;
    outStreamState.wBitsPerSample = 16;
    outStreamState.nBlockAlign = (outStreamState.wBitsPerSample >> 3)*outStreamState.nChannels;
    outStreamState.nAvgBytesPerSec = outStreamState.nSamplesPerSec*outStreamState.nBlockAlign;
    outStreamState.cbSize = 0;

    // Create the output device
    MMRESULT result = 
        waveOutOpen( &m_outDevice, WAVE_MAPPER, &outStreamState, m_dwAudioThreadId, NULL, 
                      CALLBACK_THREAD | WAVE_FORMAT_DIRECT);

    if ( result != MMSYSERR_NOERROR )
        m_outDevice = NULL;

    // Zero out the output block headers
    ZeroMemory(m_outputHeaders, 2*sizeof(WAVEHDR));
    // Set the user data to DMA2
    m_outputHeaders[0].dwUser = SOURCE_DMA2;
    m_outputHeaders[1].dwUser = SOURCE_DMA2;
    m_outputHeaders[0].lpData = (LPSTR)&OutputBuffer[0];
    m_currOutputHeader = &m_outputHeaders[0];
    m_outputDMA = false;
    m_SwitchOutputQueue = false;

    // Create the input device
    result = waveInOpen( &m_inDevice, WAVE_MAPPER, &outStreamState, m_dwAudioThreadId, NULL, 
                         CALLBACK_THREAD );

    if ( result != MMSYSERR_NOERROR )
        m_inDevice = NULL;

    // Zero out the input block headers
    ZeroMemory(m_inputHeaders, 2*sizeof(WAVEHDR));
    // Set the user data to DMA2
    m_inputHeaders[0].dwUser = SOURCE_DMA1;
    m_inputHeaders[1].dwUser = SOURCE_DMA1;
    m_inputHeaders[0].lpData = (LPSTR)&InputBuffer[0];
    m_currInputHeader = &m_inputHeaders[0];
    m_inputDMA = false;
    m_SwitchInputQueue = false;

    // Check if the DMA controller has already enabled input (across saved state)
    if (DMAController1.ReadWord(0x20) & 0x2 )
        SetInputDMA(true);

#ifdef AUDIO_TESTING
    // Write out the header of the wave file
    WaveChunk fmt;
    if( (output_file = fopen("temp.wav","w"))==NULL){
        return false;
    }
    else
    {
        output_block_count = 0;
        fmt.groupID[0] = 'R';fmt.groupID[1] = 'I';fmt.groupID[2] = 'F'; fmt.groupID[3]= 'F';
        fmt.groupID[4]= 0x28; fmt.groupID[5]= 0x8f; fmt.groupID[6]= 0; fmt.groupID[7]= 0;
        fmt.riffTypeID[0] = 'W';fmt.riffTypeID[1]='A';fmt.riffTypeID[2]='V'; fmt.riffTypeID[3] ='E';
        fmt.chunkID[0] = 'f';fmt.chunkID[1] = 'm';fmt.chunkID[2] = 't'; fmt.chunkID[3] = ' ';
        fmt.chunkSize = 0x10;
        fmt.wFormatTag = 1;
        fmt.wChannels = 2;
        fmt.dwSamplesPerSec = 44100;
        fmt.dwAvgBytesPerSec = 4*44100;
        fmt.wBlockAlign = 4;
        fmt.wBitsPerSample = 16;
        fmt.dataID[0] = 'd';fmt.dataID[1]='a';fmt.dataID[2]='t'; fmt.dataID[3] = 'a';
        fmt.dataSize = 0x800*BLOCK_SIZE;
        fwrite( &fmt, sizeof(WaveChunk), 1, output_file );
    }
#endif

    return true;
}

IOIIS::~IOIIS()
{
#ifdef AUDIO_TESTING
    fclose(output_file);
#endif
}

DWORD WINAPI IOIIS::AudioIOLoopStatic(LPVOID lpvThreadParam)
{
    return ((IOIIS*)lpvThreadParam)->AudioIOLoop();
}

void IOIIS::FlushOutput()
{
    // If the other queue is not playing, play the current one
    if ( m_currOutputHeader->dwBufferLength != 0 && !m_SwitchOutputQueue)
    {
        if (OutputQueueSwitchPossible())
        {
            PlayOutputQueue();
            SwitchOutputQueue();
        }
    }
}

void IOIIS::SetOutputDMA(bool on )
{
    if ( on )
    {
        // Post a message enabling output interrupts, all messages
        // prior to this will not generate interrupts
        PostThreadMessage(m_dwAudioThreadId, MM_WOM_DMAENABLE, NULL, NULL );
    }
    else
    {
        // The DMA channel must be off prior to flipping of the IIS DMA
        ASSERT(!(DMAController2.ReadWord(0x20) & 0x2));
        m_outputDMA = false;
    }
}

void IOIIS::DisableOutput()
{
    // The state machine is too complex, so the chances of recovery are small
    // exit with internal error message
    TerminateWithMessage(ID_MESSAGE_INTERNAL_ERROR);
}

void IOIIS::SetInputDMA(bool on )
{
    LOG_INFO(AUDIO, "Turned input %d.", on);
    if ( on )
    {
        // Post a message enabling output interrupts, all messages
        // prior to this will not generate interrupts
        PostThreadMessage(m_dwAudioThreadId, MM_WIM_DMAENABLE, NULL, NULL );
        waveInStart(m_inDevice);
    }
    else
    {
        // Either the DMA channel must be off prior to flipping of the IIS DMA
        // Or the DMA transfer should be stopped
        ASSERT(!(DMAController1.ReadWord(0x20) & 0x2) || (DMAController1.ReadWord(0x20) & 0x4));
        m_inputDMA = false;
        waveInStop(m_inDevice);
    }
}

void IOIIS::DisableInput()
{
    // The state machine is too complex, so the chances of recovery are small
    // exit with internal error message
    TerminateWithMessage(ID_MESSAGE_INTERNAL_ERROR);
}

bool IOIIS::OutputQueueSwitchPossible()
{
    // Figure out the queue that is not being used
    WAVEHDR * header = m_currOutputHeader == &m_outputHeaders[0] ? &m_outputHeaders[1]: &m_outputHeaders[0];
    // Check if it is unprepared
    return (!(header->dwFlags & WHDR_PREPARED));
}

void IOIIS::SwitchOutputQueue()
{
    if (OutputQueueSwitchPossible())
    {
        // Switch to the other queue for output
        m_currOutputHeader = (m_currOutputHeader == &m_outputHeaders[0] ? 
                                                &m_outputHeaders[1]: &m_outputHeaders[0]);
        // Reset the header for the queue
        ResetCurrentOutputQueue();
    }
    else
    {
        m_SwitchOutputQueue = true;
        m_currOutputHeader = (m_currOutputHeader == &m_outputHeaders[0] ? 
                                                    &m_outputHeaders[1]: &m_outputHeaders[0]);
    }
}

void IOIIS::ResetCurrentOutputQueue()
{
    ASSERT( !(m_currOutputHeader->dwFlags & WHDR_PREPARED) ); 
    WAVEHDR * header = m_currOutputHeader;

    // Zero out all of the header except for the dwUser field which is initialized at startup
    ZeroMemory(header, (int)((BYTE *)&(header->dwUser)-(BYTE *)header) ); 
    ZeroMemory((BYTE *)&(header->dwUser) + sizeof(header->dwUser), 
                sizeof(WAVEHDR)-(int)((BYTE *)&(header->dwUser)-(BYTE *)header) - sizeof(header->dwUser));
    //Set the address to the appropriate output buffer
    header->lpData = (LPSTR)(header == &m_outputHeaders[0] ? 
                       &OutputBuffer[0]: &OutputBuffer[BLOCK_SIZE*QUEUE_LENGTH] );
}

void IOIIS::PlayOutputQueue()
{
    // Check if the current header is correct before passing it to waveOut
    ASSERT( m_currOutputHeader == &m_outputHeaders[0] || m_currOutputHeader == &m_outputHeaders[1]);
    ASSERT( m_currOutputHeader->reserved == 0 && m_currOutputHeader->dwFlags == 0 );
    ASSERT( m_currOutputHeader->dwBufferLength > 0 );
    ASSERT( m_currOutputHeader->lpData == (LPSTR)(m_currOutputHeader == &m_outputHeaders[0] ? 
                                          &OutputBuffer[0]: &OutputBuffer[BLOCK_SIZE*QUEUE_LENGTH]) );

    // Prepare the header and write it out to the audio device
    MMRESULT result = waveOutPrepareHeader(m_outDevice, m_currOutputHeader, sizeof(WAVEHDR));
    if ( result != MMSYSERR_NOERROR )
    {
        ASSERT(false && !"Couldn't prepare the buffer wave output \n");
        DisableOutput();
    }

    result = waveOutWrite(m_outDevice, m_currOutputHeader, sizeof(WAVEHDR));
    if ( result != MMSYSERR_NOERROR )
    {
        ASSERT(false && !"Couldn't play the buffer wave output \n");
        DisableOutput();
    }
}

void IOIIS::QueueOutput(void * address, unsigned int size)
{
    WAVEHDR * header = m_currOutputHeader;

    // If address is NULL we must have been unable to map ARM address to x86
    if (address == NULL || size != BLOCK_SIZE)
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);

    // If audio is enabled, queue the buffer in the current queue
    if ( m_outDevice != NULL )
    {

        if ( m_SwitchOutputQueue )
        {
            if ( !(m_currOutputHeader->dwFlags & WHDR_PREPARED) )
            {
                ResetCurrentOutputQueue();
                m_SwitchOutputQueue = false;
            }
            else
            {
                // We drop the packet here because CE got ahead of us
                LOG_WARN(AUDIO, "Droping an audio packet");
                return;
            }
        }

        ASSERT( header->dwBufferLength >= 0 && header->dwBufferLength < BLOCK_SIZE*QUEUE_LENGTH );
        memcpy( (char *)header->lpData + header->dwBufferLength, address, BLOCK_SIZE);
        header->dwBufferLength += BLOCK_SIZE;

        if ( header->dwBufferLength == BLOCK_SIZE*QUEUE_LENGTH )
        {

            PlayOutputQueue();

#ifdef AUDIO_TESTING
            if (output_block_count < OUTPUT_BLOCK_COUNT )
            {
                printf( "Output block %x \n", output_block_count );
                fwrite(address, BLOCK_SIZE*QUEUE_LENGTH, 1, output_file );
                output_block_count++;
            }
#endif

            SwitchOutputQueue();
        }

        if ( !m_SwitchOutputQueue )
            PostThreadMessage(m_dwAudioThreadId, MM_WOM_DONE, NULL, (LPARAM)header );
    } 
    else // if ( m_outDevice == NULL )
    {
        // If audio is disabled post a message triggering the interrupt on the audioloop thread
        PostThreadMessage(m_dwAudioThreadId, MM_WOM_DONE, NULL, (LPARAM)header );
    }
}

bool IOIIS::InputQueueSwitchPossible()
{
    // Figure out the queue that is not being used
    WAVEHDR * header = m_currInputHeader == &m_inputHeaders[0] ? &m_inputHeaders[1]: &m_inputHeaders[0];
    // Check if it is unprepared
    return (!(header->dwFlags & WHDR_PREPARED));
}


void IOIIS::SwitchInputQueue()
{
    if (!InputQueueSwitchPossible())
    {
        LOG_INFO(AUDIO, "No buffer was available. Setting m_SwitchInputQueue to true");
        m_SwitchInputQueue = true;
    }

    // Switch to the other queue for Input
    m_currInputHeader = (m_currInputHeader == &m_inputHeaders[0] ? 
                                              &m_inputHeaders[1]: &m_inputHeaders[0]);
}

void IOIIS::ResetCurrentInputQueue()
{
    ASSERT( !(m_currInputHeader->dwFlags & WHDR_PREPARED) ); 
    WAVEHDR * header = m_currInputHeader;

    // Zero out all of the header except for the dwUser field which is initialized at startup
    ZeroMemory(header, (int)((BYTE *)&(header->dwUser)-(BYTE *)header) ); 
    ZeroMemory((BYTE *)&(header->dwUser) + sizeof(header->dwUser), 
                sizeof(WAVEHDR)-(int)((BYTE *)&(header->dwUser)-(BYTE *)header) - sizeof(header->dwUser));
    //Set the address to the appropriate Input buffer
    header->lpData = (LPSTR)(header == &m_inputHeaders[0] ? 
                       &InputBuffer[0]: &InputBuffer[BLOCK_SIZE*QUEUE_LENGTH] );
    header->dwBufferLength = BLOCK_SIZE*QUEUE_LENGTH;
}

void IOIIS::RecordInputQueue()
{
    WAVEHDR * header = m_currInputHeader;

    // Check if the current header is correct before passing it to waveIn
    ASSERT( m_currInputHeader == &m_inputHeaders[0] || m_currInputHeader == &m_inputHeaders[1]);
    ASSERT( m_currInputHeader->reserved == 0 && m_currInputHeader->dwFlags == 0 );
    ASSERT( m_currInputHeader->dwBufferLength == BLOCK_SIZE*QUEUE_LENGTH);
    ASSERT( m_currInputHeader->lpData == (LPSTR)(m_currInputHeader == &m_inputHeaders[0] ? 
                                          &InputBuffer[0]: &InputBuffer[BLOCK_SIZE*QUEUE_LENGTH]) );


    MMRESULT result = waveInPrepareHeader(m_inDevice, header, sizeof(WAVEHDR));
    if ( result != MMSYSERR_NOERROR )
    {
        ASSERT(false && !"Couldn't prepare the buffer for wave input \n");
        DisableInput();
    }

    result =  waveInAddBuffer(m_inDevice, header, sizeof(WAVEHDR));

    if ( result != MMSYSERR_NOERROR )
    {
        ASSERT(false && !"Couldn't add the buffer to wave input \n");
        DisableInput();
    }
}

void IOIIS::QueueInput(void * address, unsigned int size)
{
    WAVEHDR * header = m_currInputHeader;

    // If address is NULL we must have been unable to map ARM address to x86
    if (address == NULL || size != BLOCK_SIZE)
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);

    if ( m_inDevice != NULL )
    {

        if ( m_SwitchInputQueue )
        {
            if ( !(m_currInputHeader->dwFlags & WHDR_PREPARED))
            {
                ASSERT( m_currInputHeader->dwBufferLength == BLOCK_SIZE*QUEUE_LENGTH);
                m_SwitchInputQueue = false;
            }
            else
            {
                // Return silence here because CE got ahead of us
                LOG_WARN(AUDIO, "Returning an empty input buffer");
                ZeroMemory(address, size ); 
                PostThreadMessage(m_dwAudioThreadId, MM_WIM_DATA, NULL, (LPARAM)header );
                return;
            }
        }

        if ( header->dwBufferLength == 0 )
        {
            // Return silence here because we have no data on the first request
            ZeroMemory(address, size ); 
        }
        else
        {
            // Grab some data out of the buffer and pass it to guest OS
            ASSERT( header->dwBufferLength > 0 && header->dwBufferLength <= BLOCK_SIZE*QUEUE_LENGTH );
            memcpy( address, (char *)header->lpData + 
                             (BLOCK_SIZE*QUEUE_LENGTH - header->dwBufferLength), BLOCK_SIZE);
            header->dwBufferLength -= BLOCK_SIZE;
        }

        if ( header->dwBufferLength <= 0 )
        {
            ASSERT( header->dwBufferLength == 0 );
            // Attempt to refil the queue with audio data
            ResetCurrentInputQueue();
            RecordInputQueue();
            SwitchInputQueue();
        }

        if ( !m_SwitchInputQueue )
            PostThreadMessage(m_dwAudioThreadId, MM_WIM_DATA, NULL, (LPARAM)header );

    }
    else if ( m_inDevice == NULL )
    {
        // If audio is disabled post a message triggering the interrupt on the audioloop thread
        PostThreadMessage(m_dwAudioThreadId, MM_WIM_DATA, NULL, (LPARAM)header );
    }
}

DWORD WINAPI IOIIS::AudioIOLoop(void)
{
    MSG Msg;

    while (GetMessage(&Msg, NULL, 0, 0)) 
    {
        if ( Msg.message == MM_WOM_DMAENABLE)
            m_outputDMA = true;
        else if ( Msg.message == MM_WIM_DMAENABLE)
            m_inputDMA = true;
        else if ( Msg.message == MM_WOM_DONE || Msg.message == MM_WIM_DATA)
        {
            // If the wave header is null something has gone very wrong
            if (Msg.lParam == NULL)
                TerminateWithMessage(ID_MESSAGE_INTERNAL_ERROR);

            // Unprepare the block
            MMRESULT result = MMSYSERR_NOERROR;
            if ( Msg.message == MM_WOM_DONE && Msg.wParam != NULL && m_outDevice != NULL )
                result = waveOutUnprepareHeader(m_outDevice, (LPWAVEHDR)Msg.lParam, sizeof(WAVEHDR));
            else if ( Msg.message == MM_WIM_DATA && Msg.wParam != NULL && m_inDevice != NULL)
                result = waveInUnprepareHeader(m_inDevice, (LPWAVEHDR)Msg.lParam, sizeof(WAVEHDR));

            if ( result != MMSYSERR_NOERROR )
            {
                ASSERT(false && !"Couldn't unprepare the audio data buffer\n");
                // Manually unprepare the buffer so we can appear to make progress
                ((LPWAVEHDR)Msg.lParam)->dwFlags &= ~WHDR_PREPARED;
            }

            // If audio is disabled delay the interrupt so that the driver has time
            // to fill the buffer
            if ( Msg.wParam == NULL && 
                ( (Msg.message == MM_WOM_DONE && m_outDevice == NULL) ||
                  ( Msg.message == MM_WIM_DATA && m_inDevice == NULL) ))
                Sleep(4);

            // Trigger the interrupt if the DMA channel is still on
            WAVEHDR * header = (LPWAVEHDR)Msg.lParam;
            EnterCriticalSection(&IOLock);
Retry:
            if ( header->dwUser & SOURCE_DMA1 && DMAController1.ReadWord(0x20) & 0x2)
            {
                if ( m_inputDMA && ( Msg.wParam == NULL || m_SwitchInputQueue )) // Interrupt is not surpressed
                {
                    if ( InterruptController.IsPending(InterruptController.SourceINT_DMA1 ) )
                    {
                        LeaveCriticalSection(&IOLock);
                        Sleep(4);
                        EnterCriticalSection(&IOLock);
                        goto Retry;
                    }
                    InterruptController.RaiseInterrupt(InterruptController.SourceINT_DMA1);
                    if ( Msg.wParam != NULL && m_SwitchInputQueue)
                        m_SwitchInputQueue = false;
                }
            }
            else if ( header->dwUser & SOURCE_DMA2 )
            {
                if ( DMAController2.ReadWord(0x20) & 0x2 )
                {
                    if ( m_outputDMA && ( Msg.wParam == NULL || m_SwitchOutputQueue )) // Interrupt is not surpressed
                        InterruptController.RaiseInterrupt(InterruptController.SourceINT_DMA2);
                }
                else if (Msg.wParam != NULL && m_outDevice != NULL)
                    FlushOutput();
            }
            LeaveCriticalSection(&IOLock);
        }
    }

    // Close the device since we are exiting
    if ( m_outDevice != NULL )
        waveOutClose( m_outDevice );
    if ( m_inDevice != NULL )
        waveInClose( m_inDevice );

    return 0;
}

void __fastcall IOIIS::SaveState(StateFiler& filer) const
{
    filer.Write('IIIS');
    filer.Write(IISCON.HalfWord);
    filer.Write(IISMOD.HalfWord);
    filer.Write(IISPSR.HalfWord);
    filer.Write(IISFCON.HalfWord);
    filer.Write(IISFIFO.HalfWord);
}

void __fastcall IOIIS::RestoreState(StateFiler& filer)
{
    filer.Verify('IIIS');
    filer.Read(IISCON.HalfWord);
    filer.Read(IISMOD.HalfWord);
    filer.Read(IISPSR.HalfWord);
    filer.Read(IISFCON.HalfWord);
    filer.Read(IISFIFO.HalfWord);
}

unsigned __int16 __fastcall IOIIS::ReadHalf(unsigned __int32 IOAddress){
    TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
    return 0;    
}
unsigned __int32 __fastcall IOIIS::ReadWord(unsigned __int32 IOAddress){
    switch(IOAddress){
        case 0x0:    return (IISCON.HalfWord | TRANSMIT_FIFO_READY | RECEIVE_FIFO_READY );
        case 0x4:    return IISMOD.HalfWord;
        case 0x8:    return IISPSR.HalfWord;
        case 0xc:    return IISFCON.HalfWord;
        case 0x10:    return IISFIFO.HalfWord;
        default: TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE); break;
    }
    return 0;
}
void __fastcall IOIIS::WriteHalf(unsigned __int32 IOAddress, unsigned __int16 Value){
    TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
}
void __fastcall IOIIS::WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value){
    //printf("Writing to %x value %x\n", IOAddress, Value );
    switch(IOAddress){
        case 0x0:    IISCON.HalfWord = (unsigned __int16)Value;    break;
        case 0x4:    IISMOD.HalfWord = (unsigned __int16)Value;    break;
        case 0x8:    IISPSR.HalfWord = (unsigned __int16)Value;    break;
        case 0xc:    IISFCON.HalfWord = (unsigned __int16)Value;    break;
        case 0x10:    IISFIFO.HalfWord = (unsigned __int16)Value;    break;
        default:    TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE); break;
    }
}

void __fastcall IOClockAndPower::SaveState(StateFiler& filer) const
{
    filer.Write('CLKP');
    filer.Write(LOCKTIME.Word);
    filer.Write(MPLLCON.Word);
    filer.Write(UPLLCON.Word);
    filer.Write(CLKCON.Word);
    filer.Write(CLKSLOW.Byte);
    filer.Write(CLKDIVN.Byte);
}

void __fastcall IOClockAndPower::RestoreState(StateFiler& filer)
{
    filer.Verify('CLKP');
    filer.Read(LOCKTIME.Word);
    filer.Read(MPLLCON.Word);
    filer.Read(UPLLCON.Word);
    filer.Read(CLKCON.Word);
    filer.Read(CLKSLOW.Byte);
    filer.Read(CLKDIVN.Byte);
}

unsigned __int16 __fastcall IOClockAndPower::ReadHalf(unsigned __int32 IOAddress){
    TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
    return 0;
}
unsigned __int32 __fastcall IOClockAndPower::ReadWord(unsigned __int32 IOAddress){
    switch(IOAddress){
        case 0x0:    return LOCKTIME.Word;
        case 0x4:    return MPLLCON.Word;
        case 0x8:    return UPLLCON.Word;
        case 0xc:    return CLKCON.Word;
        case 0x10:    return CLKSLOW.Byte;
        case 0x14:    return CLKDIVN.Byte;
        default:    TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE); break;
    }
    return 0;
}
void __fastcall IOClockAndPower::WriteHalf(unsigned __int32 IOAddress, unsigned __int16 Value){
    TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
}
void __fastcall IOClockAndPower::WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value){
    switch(IOAddress){
        case 0x0:    LOCKTIME.Word = Value; break;
        case 0x4:    MPLLCON.Word = Value; break;
        case 0x8:    UPLLCON.Word = Value; break;
        case 0xc:    CLKCON.Word = Value; break;
        case 0x10:    CLKSLOW.Byte = (unsigned __int8)Value; break;
        case 0x14:    CLKDIVN.Byte = (unsigned __int8)Value; break;
    }
}

void __fastcall IOMemoryController::SaveState(StateFiler& filer) const
{
    filer.Write('MEMC');
    filer.Write(BWSCON.Word);
    filer.Write(BANKCON0.HalfWord);
    filer.Write(BANKCON1.HalfWord);
    filer.Write(BANKCON2.HalfWord);
    filer.Write(BANKCON3.HalfWord);
    filer.Write(BANKCON4.HalfWord);
    filer.Write(BANKCON5.HalfWord);
    filer.Write(BANKCON6.Word);
    filer.Write(BANKCON7.Word);
    filer.Write(REFRESH.Word);
    filer.Write(BANKSIZE.Byte);
    filer.Write(MRSRB6.HalfWord);
    filer.Write(MRSRB7.HalfWord);
}

void __fastcall IOMemoryController::RestoreState(StateFiler& filer)
{
    filer.Verify('MEMC');
    filer.Read(BWSCON.Word);
    filer.Read(BANKCON0.HalfWord);
    filer.Read(BANKCON1.HalfWord);
    filer.Read(BANKCON2.HalfWord);
    filer.Read(BANKCON3.HalfWord);
    filer.Read(BANKCON4.HalfWord);
    filer.Read(BANKCON5.HalfWord);
    filer.Read(BANKCON6.Word);
    filer.Read(BANKCON7.Word);
    filer.Read(REFRESH.Word);
    filer.Read(BANKSIZE.Byte);
    filer.Read(MRSRB6.HalfWord);
    filer.Read(MRSRB7.HalfWord);
}

unsigned __int16 __fastcall IOMemoryController::ReadHalf(unsigned __int32 IOAddress){
    switch(IOAddress){
        case 0x0:    return (unsigned __int16)(BWSCON.Word);
        case 0x4:    return BANKCON0.HalfWord;
        case 0x8:    return BANKCON1.HalfWord;
        case 0xc:    return BANKCON2.HalfWord;
        case 0x10:    return BANKCON3.HalfWord;
        case 0x14:    return BANKCON4.HalfWord;
        case 0x18:    return BANKCON5.HalfWord;
        case 0x1c:    return (unsigned __int16)BANKCON6.Word;
        case 0x20:    return (unsigned __int16)BANKCON7.Word;
        case 0x24:    return (unsigned __int16)REFRESH.Word;
        case 0x28:    return BANKSIZE.Byte;
        case 0x2c:    return MRSRB6.HalfWord;
        case 0x30:    return MRSRB7.HalfWord;
        default:    TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE); break;
    }
    return 0;
}
unsigned __int32 __fastcall IOMemoryController::ReadWord(unsigned __int32 IOAddress){
    switch(IOAddress){
        case 0x0:    return BWSCON.Word;
        case 0x4:    return BANKCON0.HalfWord;
        case 0x8:    return BANKCON1.HalfWord;
        case 0xc:    return BANKCON2.HalfWord;
        case 0x10:    return BANKCON3.HalfWord;
        case 0x14:    return BANKCON4.HalfWord;
        case 0x18:    return BANKCON5.HalfWord;
        case 0x1c:    return BANKCON6.Word;
        case 0x20:    return BANKCON7.Word;
        case 0x24:    return REFRESH.Word;
        case 0x28:    return BANKSIZE.Byte;
        case 0x2c:    return MRSRB6.HalfWord;
        case 0x30:    return MRSRB7.HalfWord;
        default:    TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE); break;
    }
    return 0;
}
void __fastcall IOMemoryController::WriteHalf(unsigned __int32 IOAddress, unsigned __int16 Value){
    switch(IOAddress){
        case 0x0:    BWSCON.Word = Value; break;
        case 0x4:    BANKCON0.HalfWord = Value;    break;
        case 0x8:    BANKCON1.HalfWord = Value;    break;
        case 0xc:    BANKCON2.HalfWord = Value;    break;
        case 0x10:    BANKCON3.HalfWord = Value;    break;
        case 0x14:    BANKCON4.HalfWord = Value;    break;
        case 0x18:    BANKCON5.HalfWord = Value;    break;
        case 0x1c:    BANKCON6.Word = Value;    break;
        case 0x20:    BANKCON7.Word = Value;    break;
        case 0x24:    REFRESH.Word = Value; break;
        case 0x28:    BANKSIZE.Byte = (unsigned __int8)Value;    break;
        case 0x2c:    MRSRB6.HalfWord = (unsigned __int16)Value;    break;
        case 0x30:    MRSRB7.HalfWord = (unsigned __int16)Value;    break;
        default: TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE); break;
    }
}
void __fastcall IOMemoryController::WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value){
    switch(IOAddress){
        case 0x0:    BWSCON.Word = Value; break;
        case 0x4:    BANKCON0.HalfWord = (unsigned __int16)Value;    break;
        case 0x8:    BANKCON1.HalfWord = (unsigned __int16)Value;    break;
        case 0xc:    BANKCON2.HalfWord = (unsigned __int16)Value;    break;
        case 0x10:    BANKCON3.HalfWord = (unsigned __int16)Value;    break;
        case 0x14:    BANKCON4.HalfWord = (unsigned __int16)Value;    break;
        case 0x18:    BANKCON5.HalfWord = (unsigned __int16)Value;    break;
        case 0x1c:    BANKCON6.Word = Value;    break;
        case 0x20:    BANKCON7.Word = Value;    break;
        case 0x24:    REFRESH.Word = Value; break;
        case 0x28:    BANKSIZE.Byte = (unsigned __int8)Value;    break;
        case 0x2c:    MRSRB6.HalfWord = (unsigned __int16)Value;    break;
        case 0x30:    MRSRB7.HalfWord = (unsigned __int16)Value;    break;
        default:    TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE); break;
    }
}

void __fastcall IOWatchDogTimer::WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value){
    // Note: 1.  kernel\hal\arm\fw.s appears to just be disabling watchdogtimer (by setting WTCON=0), so just ignoring it for now
    //       2.  kernel\hal\oemioctl.c sets WTDAT=0, WTCNT=0 and WTCON=8021 to initiate a
    //           device reset.  Immediately following the write to TCON is a C "while (TRUE);" loop
    //           that expects to be interrupted when the watchdog timer fires.
    switch (IOAddress) {
    case 0:        // WTCON
        if ((Value & 0x21) == 0x21) {
            // Reset enabled and timer is enabled.  Simulate assertion of the CPU's
            // RESET pin by telling the CPU to reset itself shortly after we return.
            BoardReset(false);
        }
        break;

    case 4:        // WTDAT
        break;

    case 8:        // WTCNT
        break;

    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
    }
}


void __fastcall IORealTimeClock::SaveState(StateFiler& filer) const
{
    filer.Write('RTCC');
    filer.Write(RTCCON);
    filer.Write(m_ftRTCAdjust);
    filer.Write(m_stRTC);
}

void __fastcall IORealTimeClock::RestoreState(StateFiler& filer)
{
    filer.Verify('RTCC');
    filer.Read(RTCCON);
    filer.Read(m_ftRTCAdjust);
    filer.Read(m_stRTC);
}

unsigned __int32 __fastcall IORealTimeClock::ReadWord(unsigned __int32 IOAddress){
    SYSTEMTIME s;
    FILETIME ftNow;
    GetLocalTime(&s);
    // If we fail in adjusting the time - we'll return host time
    if (SystemTimeToFileTime(&s, &ftNow))
    {
        *(unsigned __int64*)&ftNow += *(unsigned __int64*)&m_ftRTCAdjust;
        FileTimeToSystemTime(&ftNow, &s);
    }

    switch(IOAddress){
        case 0x0:    return RTCCON.Byte;
        case 0x30:    return TO_BCD(s.wSecond);
        case 0x34:    return TO_BCD(s.wMinute);
        case 0x38:    return TO_BCD(s.wHour);
        case 0x3c:    return TO_BCD(s.wDay);
        case 0x40:    return TO_BCD(s.wDayOfWeek);
        case 0x44:    return TO_BCD(s.wMonth);
        case 0x48:    return TO_BCD(s.wYear-YEAR_ADJUST);
        default:    TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE); break;
    }
    return 0;
}

void __fastcall IORealTimeClock::WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value){
    switch(IOAddress){
        case 0x0:
            if (RTCCON.Byte && Value == 0) {
                // Disabling RTC control.
                // This means that writes to the alarm time and/or clock times have completed.

                if (m_fRTCWritten) {
                    // The RTC was written, indicating that the guest wants to change the system time
                    // For some reason, CE42 writes to the RTC several times during boot, specifying
                    // year 2079.  Ignore those writes.
                    SYSTEMTIME stNow;
                    FILETIME ftNow;
                    FILETIME ftRTCTime;

                    GetLocalTime(&stNow);
                    if ( SystemTimeToFileTime(&stNow, &ftNow) )
                    {
                        m_stRTC.wMilliseconds = 0;
                        if (SystemTimeToFileTime(&m_stRTC, &ftRTCTime))
                            *(unsigned __int64*)&m_ftRTCAdjust = *(unsigned __int64*)&ftRTCTime - *(unsigned __int64*)&ftNow;
                    }
                    m_fRTCWritten = false;
                }
            }
            RTCCON.Byte = (unsigned __int8) Value;
            break;
        case 0x10:  
        case 0x14:  // The ALM registers are written by OEMSetAlarmTime(), but are never
        case 0x18:  // written, and the RTCALM is never written, so the alarm interrupt
        case 0x1c:  // is never generated.  Just make these writes NOPs.
        case 0x20:
        case 0x24:
        case 0x28:
        case 0x2c:  // RTCRST
            break;
        case 0x30: 
            m_stRTC.wSecond = FROM_BCD(Value);
            m_fRTCWritten = true;
            break;
        case 0x34:
            m_stRTC.wMinute = FROM_BCD(Value);
            m_fRTCWritten = true;
            break;
        case 0x38:    
            m_stRTC.wHour = FROM_BCD(Value);
            m_fRTCWritten = true;
            break;
        case 0x3c:
            m_stRTC.wDay = FROM_BCD(Value);
            m_fRTCWritten = true;
            break;
        case 0x40:
            m_stRTC.wDayOfWeek = FROM_BCD(Value);
            m_fRTCWritten = true;
            break;
        case 0x44:
            m_stRTC.wMonth = FROM_BCD(Value);
            m_fRTCWritten = true;
            break;
        case 0x48:
            m_stRTC.wYear = FROM_BCD(Value) + YEAR_ADJUST;
            m_fRTCWritten = true;
            break;
        default:    TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE); break;
    }
}


bool __fastcall IOHardwareUART::PowerOn()
{
    OverlappedWrite.hEvent = 0;
    OverlappedRead.hEvent = 0;
    OverlappedCommEvent.hEvent = 0;
    Callback.lpRoutine = CompletionRoutineStatic;
    Callback.lpParameter = this;
    fShouldWaitForNextCommEvent = true;
    hCom = INVALID_HANDLE_VALUE;

    if ( Configuration.getUART(DeviceNumber) != NULL &&
       ( !Configuration.NoSecurityPrompt && 
         !PromptToAllow( ID_MESSAGE_ENABLE_SERIAL, DeviceNumber, Configuration.getUART(DeviceNumber) ) ) )
    {
        Configuration.setUART(DeviceNumber, NULL);
    }
        
    if (!Reconfigure(Configuration.getUART(DeviceNumber))) {
        return false;
    }

    // Simulate an empty Tx buffer
    UTRSTAT.Bits.TransmitBufferEmpty=1;
    UTRSTAT.Bits.TransmitEmpty=1;

    // Raise a TX-ready interrupt:  we're ready to transmit a byte
    InterruptController.RaiseInterrupt(TxInterruptSubSource);
    return true;
}

bool __fastcall IOHardwareUART::Reconfigure(__in_z const wchar_t * NewParam)
{
    if (NewParam) {
        bool fSetWin32ComPortSettings;

        // Binding the UART to a Win32 COM: port...
        if (hCom == INVALID_HANDLE_VALUE) {
            // The "easy" case - going from not-connected to connected...
            fSetWin32ComPortSettings = false;
        } else {
            // Going from connected to connected to something else - preserve more COM port settings
            fSetWin32ComPortSettings = true;
            HANDLE OldCom = hCom;
            hCom = INVALID_HANDLE_VALUE;
            CloseHandle(OldCom);
        }

        //
        // Open the serial port.  It must be opened with FILE_FLAG_OVERLAPPED, or else
        // one of the serial I/O APIs will block when called.  I can't recall which one,
        // and the MSDN docs are spotty about documenting this blocking behavior.
        //
        hCom = CreateFileW(NewParam,
            GENERIC_READ|GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            NULL);
        if (hCom == INVALID_HANDLE_VALUE) {
            wchar_t lastErrorBuffer[1000];
            DecodeLastError(GetLastError(), 1000, lastErrorBuffer);
            ShowDialog(ID_MESSAGE_UNABLE_TO_OPEN_SERIAL, NewParam, lastErrorBuffer);
            return false;
        }

        // Flush any characters currently stored in the Win32 serial driver
        PurgeComm(hCom, PURGE_TXCLEAR|PURGE_RXCLEAR);

        COMMTIMEOUTS ct;
        memset(&ct, 0, sizeof(ct));
        ct.ReadIntervalTimeout = 1; // ReadFile() returns after 1ms of inactivity after characters begin to arrive
        if (SetCommTimeouts(hCom, &ct) == 0) {
            ASSERT(FALSE);
        }

        DWORD ModemStatus;
        if (GetCommModemStatus(hCom, &ModemStatus)) {
            UMSTAT.Bits.CTS = (ModemStatus & MS_CTS_ON) ? 1 : 0;
            UMSTAT.Bits.DSR = (ModemStatus & MS_DSR_ON) ? 1 : 0;
        } else {
            ASSERT(FALSE);
            return false;
        }

        if (fSetWin32ComPortSettings) {
            // UpdateComPortSettings() is a no-op if the updated 
            // values are equal to the current values.  So
            // force an update of the Win32 COM port settings by
            // grabbing the current values of ULCON/UBRDIV, zeroing
            // them, and calling UpdateComPortSettings() with the
            // cached values.  
            unsigned __int8 NewULCON = ULCON.Byte;
            unsigned __int32 NewUBRDIV = UBRDIV;
            ULCON.Byte = 0;
            UBRDIV = 0;
            UpdateComPortSettings(NewULCON, NewUBRDIV);
        }

        if (!CompletionPort.AssociateHandleWithCompletionPort(hCom, &Callback)) {
            CloseHandle(hCom);
            hCom = INVALID_HANDLE_VALUE;
            ShowDialog(ID_MESSAGE_RESOURCE_EXHAUSTED);
            return false;
        }

        // Initiate an async read
        BeginAsyncRead();

        // Initiate an async WaitCommEvent
        SetCommMask(hCom, CommMask);
        BeginAsyncWaitCommEvent();

    } else if (hCom != INVALID_HANDLE_VALUE) {
        // Unbinding from a COM port to nothing - cancel any pending I/O and
        // prevent any completion routines from doing any more work.
        HANDLE OldCom = hCom;
        hCom = INVALID_HANDLE_VALUE;
        CancelIo(OldCom);
        CloseHandle(OldCom);
    } else {
        // Unbind, but not already bound... hCOM is already INVALID_HANDLE_VALUE
        // so there is nothing to do.
    }
    return true;
}

void IOHardwareUART::CompletionRoutineStatic(LPVOID lpParameter, DWORD dwBytesTransferred, LPOVERLAPPED lpOverlapped)
{
    IOHardwareUART *pThis = (IOHardwareUART *)lpParameter;

    pThis->CompletionRoutine(dwBytesTransferred, lpOverlapped);
}

void IOHardwareUART::CompletionRoutine(DWORD dwBytesTransferred, LPOVERLAPPED lpOverlapped)
{
    if (lpOverlapped == &OverlappedRead) {
        if (dwBytesTransferred) {
            EnterCriticalSection(&IOLock);

            if (dwBytesTransferred > UART_QUEUE_LENGTH-(unsigned __int32)RxFIFOCount) {
                dwBytesTransferred = UART_QUEUE_LENGTH-RxFIFOCount;
                // The Rx FIFO is full - report the overrun error
                UERSTAT.Bits.OverrunError=true;
                if (UCON.Bits.RxErrorStatusInterruptEnable) {
                    InterruptController.RaiseInterrupt(ErrInterruptSubSource);
                }
            }

            for (DWORD i=0; i<dwBytesTransferred; ++i) {
                RxQueue[RxQueueHead]=ReceiveBuffer[i];
                RxQueueHead = (RxQueueHead+1) % UART_QUEUE_LENGTH;
                RxFIFOCount++;
            }
            UTRSTAT.Bits.ReceiveBufferDataReady=1;
            InterruptController.RaiseInterrupt(RxInterruptSubSource);

            LeaveCriticalSection(&IOLock);

            // Start another async ReadFile()
            BeginAsyncRead();
        } else {
            ASSERT(FALSE);
        }
    } else if (lpOverlapped == &OverlappedWrite) {
        EnterCriticalSection(&IOLock);
        // Nothing to do - serial port writes are nonblocking and no success/failre status is captured
        // Raise a TX-ready interrupt:  we're ready to transmit another byte
        InterruptController.RaiseInterrupt(TxInterruptSubSource);
        LeaveCriticalSection(&IOLock);
    } else if (lpOverlapped == &OverlappedCommEvent) {
        if (dwEvtMask == 0) {
            // This is likely caused by the main thread interrupting this thread
            // to make a change to the COM port settings.  Avoid the IOLock as
            // the main thread is holding it, and will continue to hold it
            // until the memory-mapped IO completes.
            return;
        }

        EnterCriticalSection(&IOLock);

        if (hCom == INVALID_HANDLE_VALUE) {
            // The user is reconfiguring the serial port - nothing needs to be done
            LeaveCriticalSection(&IOLock);
            return;
        }

        if (dwEvtMask & EV_BREAK) {
            UERSTAT.Bits.BreakDetect=1;
        }
        if (dwEvtMask & EV_CTS) {
            DWORD ModemStatus;

            UMSTAT.Bits.DeltaCTS=1;
            if (GetCommModemStatus(hCom, &ModemStatus)) {
                UMSTAT.Bits.CTS = (ModemStatus & MS_CTS_ON) ? 1 : 0;
            } else {
                ASSERT(FALSE);
            }
        }
        if (dwEvtMask & EV_DSR) {
            UMSTAT.Bits.DeltaDSR=1;
            UMSTAT.Bits.DSR=(GetDSR()) ? 1 : 0;
        }
        if (dwEvtMask & EV_ERR) {
            if (UCON.Bits.RxErrorStatusInterruptEnable) {
                InterruptController.RaiseInterrupt(ErrInterruptSubSource);
            }
        }
        LeaveCriticalSection(&IOLock);

        // Start another async WaitCommEvent
        BeginAsyncWaitCommEvent();
    } else {
        ASSERT(FALSE);
    }
}


void __fastcall IOHardwareUART::SaveState(StateFiler& filer) const
{
    filer.Write('UART');
    filer.Write(ULCON.Byte);
    filer.Write(UCON.HalfWord);
    filer.Write(UFCON.Byte);
    filer.Write(UMCON.Byte);
    filer.Write(UTRSTAT.Byte);
    filer.Write(UERSTAT.Byte);
    filer.Write(UFSTAT.HalfWord);
    filer.Write(UMSTAT.Byte);
    filer.Write(UTXH.Byte);
    filer.Write(URXH.Byte);
    filer.Write(UBRDIV);
}

void __fastcall IOHardwareUART::RestoreState(StateFiler& filer)
{
    filer.Verify('UART');
    filer.Read(ULCON.Byte);
    filer.Read(UCON.HalfWord);
    filer.Read(UFCON.Byte);
    filer.Read(UMCON.Byte);
    filer.Read(UTRSTAT.Byte);
    filer.Read(UERSTAT.Byte);
    filer.Read(UFSTAT.HalfWord);
    filer.Read(UMSTAT.Byte);
    filer.Read(UTXH.Byte);
    filer.Read(URXH.Byte);
    filer.Read(UBRDIV);
}

unsigned __int8 __fastcall IOHardwareUART::ReadByte(unsigned __int32 IOAddress){
    return (unsigned __int8)ReadWord(IOAddress);
}
unsigned __int32 __fastcall IOHardwareUART::ReadWord(unsigned __int32 IOAddress){
    switch(IOAddress){
        case 0x0:    return ULCON.Byte;
        case 0x4:    return UCON.HalfWord;
        case 0x8:    return UFCON.Byte;
        case 0xc:    return UMCON.Byte;
        case 0x10:    
            if (this == &UART1 && hCom == INVALID_HANDLE_VALUE) {
                if (_kbhit()) {
                    UTRSTAT.Bits.ReceiveBufferDataReady=1;
                    RxQueue[RxQueueHead] = (unsigned __int8)_getch();
                    RxQueueHead=(RxQueueHead+1) % UART_QUEUE_LENGTH;
                    RxFIFOCount++;
                    if (RxFIFOCount >= 15) {
                        UFSTAT.Bits.RxFIFOCount = 15;
                        UFSTAT.Bits.RxFIFOFull=1;
                    } else {
                        UFSTAT.Bits.RxFIFOCount = RxFIFOCount;
                        UFSTAT.Bits.RxFIFOFull=0;
                    }
                }
            }
            return UTRSTAT.Byte;
        case 0x14:    // UERSTAT
            {
                unsigned __int8 Byte;
                DWORD Errors;

                if (hCom != INVALID_HANDLE_VALUE) {
                    if (ClearCommError(hCom, &Errors, NULL)) {
                        if (Errors & CE_OVERRUN) {
                            UERSTAT.Bits.OverrunError=1;
                        }
                        if (Errors & CE_RXPARITY) {
                            UERSTAT.Bits.ParityError=1;
                        }
                        if (Errors & CE_FRAME) {
                            UERSTAT.Bits.FrameError=1;
                        }
                        if (Errors & CE_BREAK) {
                            UERSTAT.Bits.BreakDetect=1;
                        }
                    } else {
                        ASSERT(FALSE);
                    }
                }                
                Byte = UERSTAT.Byte;
                UERSTAT.Byte=0; // reading from UERSTAT clears the bits
                return Byte;
            }
        case 0x18:
            if (RxFIFOCount >= 16) {
                UFSTAT.Bits.RxFIFOCount = 15;
                UFSTAT.Bits.RxFIFOFull=1;
            } else {
                UFSTAT.Bits.RxFIFOCount = RxFIFOCount;
                UFSTAT.Bits.RxFIFOFull=0;
            }
            return UFSTAT.HalfWord;
        case 0x1c:
            {
                unsigned __int8 Byte;
                Byte = UMSTAT.Byte;
                UMSTAT.Bits.DeltaCTS=0; // reading from UMSTAT clears DeltaCTS
                return Byte;
            }
        case 0x20:    return UTXH.Byte;

        case 0x24:    // receive buffer register
            {
                if (UFSTAT.Bits.RxFIFOCount) {
                    URXH.Byte = RxQueue[RxQueueTail];
                    RxQueueTail = (RxQueueTail+1) % UART_QUEUE_LENGTH;
                    RxFIFOCount--;
                    ASSERT(RxFIFOCount >= 0);
                    if (RxFIFOCount==0) {
                        UTRSTAT.Bits.ReceiveBufferDataReady=0;
                    }
                } else {
                    URXH.Byte=0;
                }
                return URXH.Byte;
            }
        case 0x28:    return UBRDIV;
        default:    TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE); break;
    }
    return 0;
}

void __fastcall IOHardwareUART::WriteByte(unsigned __int32 IOAddress, unsigned __int8 Value){
    switch (IOAddress) {
        case 0x20:    // UTXH - transmit buffer register
            {
                unsigned __int8 c = (unsigned __int8)Value;
                if (hCom != INVALID_HANDLE_VALUE) {
                    // Write it to the serial port but don't block
                    BOOL b = WriteFile(hCom, &c, sizeof(c), NULL, &OverlappedWrite);
                    if(b == FALSE && GetLastError() != ERROR_IO_PENDING) {
                        ASSERT(FALSE);
                    }
                } else if (this == &UART1) { // debug UART is redirected to the console
                    putc(c, stdout);
                }
                break;
            }
        default:
            /* The switch does NOT cover all of the device IO space */
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            break;
    }
}

// The SMDK2410's serial port's divisor is set to (3145728/BaudRate)-1.
const struct {
    unsigned __int16 Divisor;
    DWORD BaudSetting;
} BaudRates[] = {
    {28596, CBR_110},
    {10484, CBR_300},
    {5241,  CBR_600},
    {2620,  CBR_1200},
    {1309,  CBR_2400},
    {654,   CBR_4800},
    {326,   CBR_9600},    // note: wince passes 329 when launching the terminal
    {217,   CBR_14400},
    {162,   CBR_19200},
    {81,    CBR_38400}, // note: the serial driver passes this value for 38400, but the kernel passes in 82.
    {55,    CBR_56000},
    {53,    CBR_57600},
    {26,    CBR_115200},
    {23,    CBR_128000},
    {11,    CBR_256000},
    {0,     0}
};

void __fastcall IOHardwareUART::UpdateComPortSettings(unsigned __int32 NewULCONValue, unsigned __int32 NewUBRDIV)
{
    if (hCom == INVALID_HANDLE_VALUE) {
        // The emulated serial port isn't connected to a Win32 COM port
        ULCON.Byte = (unsigned __int8)NewULCONValue;
        UBRDIV = (unsigned __int16)NewUBRDIV;
        return;
    }

    ULCONRegister NewULCON;

    NewULCON.Byte = (unsigned __int8)NewULCONValue;
    if (NewULCON.Byte == ULCON.Byte && NewUBRDIV == UBRDIV) {
        // Nothing needs updating
        return;
    }

    int i;
    DCB dcb;
    DWORD ModemStatus;

    // Query Windows to determine its initial state for the serial port.  But first,
    // unblock the async WaitCommEvent(), and prevents
    // other threads from calling GetCommState/SetCommState:  they block until
    // WaitCommEvent() is satisfied.  We'll force it to complete by settings the
    // comm mask to 0, which causes WaitCommEvent() to return with no bits set in
    // the event list.
    fShouldWaitForNextCommEvent = false;
    SetCommMask(hCom, 0);
    if (GetCommState(hCom, &dcb) == FALSE) {
        ASSERT(FALSE);
    }

    dcb.fDtrControl=DTR_CONTROL_ENABLE;
    dcb.fRtsControl=RTS_CONTROL_ENABLE;
    dcb.fParity=1;
    dcb.fOutxCtsFlow=0;
    dcb.fOutxDsrFlow=0;
    dcb.fDsrSensitivity=0;

    if (NewUBRDIV != UBRDIV) {
        UBRDIV = (unsigned __int16)NewUBRDIV;

        // Search for the baud rate in the table.  The requested baud rate
        // is somewhat of an estimate as it is computed several different
        // ways in various places in the WinCE codebase, and they use different
        // rounding stratgies.  Because of this, the search considers any value
        // larger than the average of the current and next baud rates as
        // close enough.
        for (i=0; i<ARRAY_SIZE(BaudRates)-1; ++i) {
            if (UBRDIV > (unsigned __int32)((BaudRates[i].Divisor+BaudRates[i+1].Divisor)/2)) {
                break;
            }
        }
        if (i == ARRAY_SIZE(BaudRates)-1) {
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE); // unknown baud rate
        } else {
            #pragma prefast(suppress:26000, "Prefast doesn't understand i<ARRAY_SIZE(BaudRates)-1")
            dcb.BaudRate = BaudRates[i].BaudSetting;
        }
    }

    if (NewULCON.Bits.ParityMode != ULCON.Bits.ParityMode) {
        ULCON.Bits.ParityMode = NewULCON.Bits.ParityMode;

        switch(ULCON.Bits.ParityMode){
            case 4:        dcb.Parity = ODDPARITY; break;
            case 5:        dcb.Parity = EVENPARITY; break;
            case 6:        ASSERT(FALSE); break; // Windows doesn't support parity forced/checked as 1
            case 7:        ASSERT(FALSE); break; // Windows doesn't support parity forced/checked as 0
            default:    dcb.Parity = NOPARITY; break;
        }
    }

    if (NewULCON.Bits.StopBits != ULCON.Bits.StopBits) {
        ULCON.Bits.StopBits = NewULCON.Bits.StopBits;

        switch(ULCON.Bits.StopBits){
            case 0:        dcb.StopBits = ONESTOPBIT; break;
            case 1:        dcb.StopBits = TWOSTOPBITS; break;
        }
    }

    if (NewULCON.Bits.WordLength != ULCON.Bits.WordLength) {
        ULCON.Bits.WordLength = NewULCON.Bits.WordLength;

        switch(ULCON.Bits.WordLength){
            case 0:        dcb.ByteSize = 5; break;
            case 1:        dcb.ByteSize = 6; break;
            case 2:        dcb.ByteSize = 7; break;
            case 3:        dcb.ByteSize = 8; break;
        }
    }

    if (SetCommState(hCom, &dcb) == FALSE) {        // Update the serial port settings
        ASSERT(FALSE);
    }
    SetCommMask(hCom, CommMask);    // Restore the event mask

    if (GetCommModemStatus(hCom, &ModemStatus)) {
        UMSTAT.Bits.CTS = (ModemStatus & MS_CTS_ON) ? 1 : 0;
        UMSTAT.Bits.DSR = (ModemStatus & MS_DSR_ON) ? 1 : 0;
    } else {
        ASSERT(FALSE);
    }

    // Initiate another async WaitCommEvent()
    fShouldWaitForNextCommEvent = true;
    BeginAsyncWaitCommEvent();
}

void __fastcall IOHardwareUART::WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value){
    switch(IOAddress){
        case 0x0:
            UpdateComPortSettings(Value, UBRDIV); // This updates ULCON
            break;
        case 0x4:    
            UCON.HalfWord = (unsigned __int16)Value;    
            if (UCON.Bits.SendBreakSignal) {
                UCON.Bits.SendBreakSignal=0;
                if (hCom != INVALID_HANDLE_VALUE) {
                    SetCommBreak(hCom);
                    ClearCommBreak(hCom);
                }
            }
            break;
        case 0x8:     
            UFCON.Byte = (unsigned __int8)Value; 
            if (UFCON.Bits.RxFIFOReset) {
                UFSTAT.Bits.RxFIFOCount=0;
                UFSTAT.Bits.RxFIFOFull=0;
                RxQueueHead = RxQueueTail;
                RxFIFOCount=0;
                UFCON.Bits.RxFIFOReset=0;
                // Leave the Rx interrupt alone, if it is already pending.  The ser2410 driver
                // is robust against this case.
            }
            if (UFCON.Bits.TxFIFOReset) {
                UFSTAT.Bits.TxFIFOCount=0;
                UFSTAT.Bits.TxFIFOFull=0;
                UFCON.Bits.TxFIFOReset=0;
            }
            break;
        case 0xc:    
            UMCON.Byte = (unsigned __int8)Value;
            if (hCom != INVALID_HANDLE_VALUE) {
                if (UMCON.Bits.RequestToSend) {
                    if (EscapeCommFunction(hCom, SETRTS) == FALSE) {
                        ASSERT(FALSE);
                    }
                } else {
                    if (EscapeCommFunction(hCom, CLRRTS) == FALSE) {
                        // WinCE's serial driver clears RTS during startup, before
                        // the Win32 serial port has been set up to allow manual
                        // RTS control.  Silently ignore the API call failure.
                        if (GetLastError() != ERROR_INVALID_PARAMETER) {
                            ASSERT(FALSE);
                        }
                    }
                }
            }
            break;
        case 0x10:    
            UTRSTAT.Byte = (unsigned __int8)Value;        
            // Simulate an empty Tx buffer
            UTRSTAT.Bits.TransmitBufferEmpty=1;
            UTRSTAT.Bits.TransmitEmpty=1;
            break;
        case 0x14:    UERSTAT.Byte = (unsigned __int8)Value;        break;
        case 0x18:    
            UFSTAT.HalfWord = (unsigned __int16)Value;    
            // Simulate an empty Tx FIFO
            UFSTAT.Bits.TxFIFOFull=0;
            UFSTAT.Bits.TxFIFOCount=0;
            break;
        case 0x1c:    UMSTAT.Byte = (unsigned __int8)Value;        break;
        case 0x20:    // transmit buffer register
            WriteByte(IOAddress, (unsigned __int8)Value);
            break;
        // can't do something on receive buffer register when writing
        case 0x24:    TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE); break;    
        case 0x28:
            UpdateComPortSettings(ULCON.Byte, Value);    // this updates UBRDIV
            break;
        default:    TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE); break;
    }
}

void IOHardwareUART::BeginAsyncRead(void)
{
    BOOL b = ReadFile(hCom, ReceiveBuffer, sizeof(ReceiveBuffer), NULL, &OverlappedRead);
    if(b == FALSE && GetLastError() != ERROR_IO_PENDING) {
        ASSERT(FALSE);
    }
}

void IOHardwareUART::BeginAsyncWaitCommEvent(void)
{
    if (fShouldWaitForNextCommEvent) {
        BOOL b = WaitCommEvent(hCom, &dwEvtMask, &OverlappedCommEvent);
        if (b == FALSE) {
            LONG err = GetLastError(); 
            // ERROR_ADAP_HDW_ERR is generated during a COM port reconfiguration
            if (err != ERROR_IO_PENDING && err != ERROR_ADAP_HDW_ERR) {
                ASSERT(FALSE);
            }
        }
    }
}

unsigned __int16 IOHardwareUART::GetLevelSubInterruptsPending(void)
{
    if (UTRSTAT.Bits.ReceiveBufferDataReady) {
        return (unsigned __int16)RxInterruptSubSource;
    } else {
        return 0;
    }
}

bool IOUART0::PowerOn(){
    DeviceNumber = 0;
    TxInterruptSubSource = InterruptController.SubSourceINT_TXD0;
    RxInterruptSubSource = InterruptController.SubSourceINT_RXD0;
    ErrInterruptSubSource= InterruptController.SubSourceINT_ERR0;
    return __super::PowerOn();
}

bool IOUART1::PowerOn(){
    DeviceNumber = 1;
    TxInterruptSubSource = InterruptController.SubSourceINT_TXD1;
    RxInterruptSubSource = InterruptController.SubSourceINT_RXD1;
    ErrInterruptSubSource= InterruptController.SubSourceINT_ERR1;
    return __super::PowerOn();
}

bool IOUART2::PowerOn(){
    DeviceNumber = 2;
    TxInterruptSubSource = InterruptController.SubSourceINT_TXD2;
    RxInterruptSubSource = InterruptController.SubSourceINT_RXD2;
    ErrInterruptSubSource= InterruptController.SubSourceINT_ERR2;
    return __super::PowerOn();
}

void IOHardwareUART::ChangeDTR(unsigned __int32 DTRValue)
{
    // Note that the logic is inverted:  if DTRValue is 0, then set
    // the DTR.  Otherwise, clear it.
    if (hCom != INVALID_HANDLE_VALUE) {
        if (EscapeCommFunction(hCom, (DTRValue) ? CLRDTR : SETDTR) == FALSE) {
            ASSERT(FALSE);
        }
    }
}

bool IOHardwareUART::GetDSR(void)
{
    DWORD ModemStatus;

    if (hCom != INVALID_HANDLE_VALUE) {
        if (GetCommModemStatus(hCom, &ModemStatus)) {
            if (ModemStatus & MS_DSR_ON) {
                return true;
            }
        } else {
            ASSERT(FALSE);
        }
    }
    return false;
}


IOInterruptController::IOInterruptController()
{
    INTMASK.Word=0xffffffff;
    INTSUBMSK.HalfWord=0x7ff;
    PRIORITY.Word=0x7f;
}

void __fastcall IOInterruptController::SaveState(StateFiler& filer) const
{
    filer.Write('INTC');
    filer.Write(SRCPND);
    filer.Write(INTMOD);
    filer.Write(INTMASK);
    filer.Write(INTPND);
    filer.Write(PRIORITY);
    filer.Write(INTOFFSET);
    filer.Write(SUBSRCPND);
    filer.Write(INTSUBMSK);
}

void __fastcall IOInterruptController::RestoreState(StateFiler& filer)
{
    filer.Verify('INTC');
    filer.Read(SRCPND);
    filer.Read(INTMOD);
    filer.Read(INTMASK);
    filer.Read(INTPND);
    filer.Read(PRIORITY);
    filer.Read(INTOFFSET);
    filer.Read(SUBSRCPND);
    filer.Read(INTSUBMSK);
}

// Aribiter group priorities.  They are indexed by PRIORTIY.ARB_SELx.  Each entry in the array
// lists the priorities in reverse order from the documentation, as the interrupt controller
// emulator will decode the priorties from lowest nibble to highest nibble.
const unsigned __int32 IOInterruptController::ArbiterPriorities[4]=
    {0x543210, 0x514320, 0x521430, 0x532140};

// ArbiterInputs[ArbiterNumber][InputNumber] contains the bit mask within the
// InterruptCollection that corresponds to ARBITERx's REQx input.
// ARBITER6 is not included in this table:  it is special-cased.
const IOInterruptController::InterruptSource IOInterruptController::ArbiterInputs[6][6]=
{
    // ARBITER0
    {SourceEINT0, SourceEINT1, SourceEINT2, SourceEINT3, Invalid, Invalid},
    // ARBITER1
    {SourceEINT4_7, SourceEINT8_23, SourceReserved1, SourcenBATT_FLT, SourceINT_TICK, SourceINT_WDT},
    // ARBITER2
    {SourceINT_TIMER0, SourceINT_TIMER1, SourceINT_TIMER2, SourceINT_TIMER3, SourceINT_TIMER4, SourceINT_UART2},
    // ARBITER3
    {SourceINT_LCD, SourceINT_DMA0, SourceINT_DMA1, SourceINT_DMA2, SourceINT_DMA3, SourceINT_SDI},
    // ARBITER4
    {SourceINT_SPI0, SourceINT_UART1, SourceReserved2, SourceINT_USBD, SourceINT_USBH, SourceINT_IIC},
    // ARBITER5
    {SourceINT_UART0, SourceINT_SPI1, SourceINT_RTC, SourceINT_ADC, Invalid, Invalid}
};

// This routine acts as the interrupt arbiter as documented in Figure 14-1
void __fastcall IOInterruptController::SetInterruptPending(void) 
{
    InterruptCollection PendingInterrupts;
    unsigned __int32 Arbiter6Priorities;

    if (INTPND.Word) {
        // An interrupt is already pending - wait for it to be delivered.
        return;
    }

    // Determine which pending IRQ interrupts are unmasked.
    PendingInterrupts.Word = ~INTMASK.Word & SRCPND.Word;
    if (PendingInterrupts.Word == 0) {
        // No interrupts are pending at this time
        return;
    }

    if (PendingInterrupts.Word & INTMOD.Word) {
        // An interrupt is pending, and it is routed to an FIQ.  Notify the CPU.
        ASSERT(FALSE);    // FIQ interrupts are not supported in the emulator:  they add unnecessary overhead to
                        // the interrupt polling code.  If they are to be re-enabled, consider doing it by
                        // having the emulator poll one Cpu.AnyInterruptPending then if it is set, check
                        // Cpu.FIQInterruptPending and Cpu.IRQInterruptPending.
        //CpuSetFIQPending();
        return;
    }
    // Otherwise, an IRQ interrupt is pending.  Sort out which one has the highest priority, and
    // report it.

    // Walk the inputs to Arbiter 6 in order according to the current ARB_SEL6 setting.
    Arbiter6Priorities=ArbiterPriorities[PRIORITY.Bits.ARB_SEL6];
    for (int Arbiter6Requests=0; Arbiter6Requests<6; ++Arbiter6Requests, Arbiter6Priorities >>= 4) {
        int Arbiter;
        int Request;
        unsigned __int32 Priorities;
        int InputCount;

        // Select the arbiter to query
        Arbiter=Arbiter6Priorities & 0xf;

        switch (Arbiter) {
        case 0:
            Priorities=ArbiterPriorities[PRIORITY.Bits.ARB_SEL0] >> 4;
            InputCount=4;
            break;

        case 1:
            Priorities=ArbiterPriorities[PRIORITY.Bits.ARB_SEL1];
            InputCount=6;
            break;

        case 2:
            Priorities=ArbiterPriorities[PRIORITY.Bits.ARB_SEL2];
            InputCount=6;
            break;

        case 3:
            Priorities=ArbiterPriorities[PRIORITY.Bits.ARB_SEL3];
            InputCount=6;
            break;

        case 4:
            Priorities=ArbiterPriorities[PRIORITY.Bits.ARB_SEL4];
            InputCount=6;
            break;

        case 5:
            Priorities=ArbiterPriorities[PRIORITY.Bits.ARB_SEL5] >> 4;
            InputCount=4;
            break;

        default:
            ASSERT(FALSE);
            InputCount=0;
            Priorities=0;
        }

        for (Request=0; Request<InputCount; ++Request) {
            unsigned __int32 InterruptBit;

            InterruptBit = ArbiterInputs[Arbiter][Request];

            if (PendingInterrupts.Word & InterruptBit) {

                // This is the highest-priority pending interrupt.  Raise it now.

                // Update the ARB_SELx to rotate as needed
                PRIORITY.Bits.ARB_SEL6++;
                switch (Arbiter) {
                case 0:
                    PRIORITY.Bits.ARB_SEL0++;
                    break;

                case 1:
                    PRIORITY.Bits.ARB_SEL1++;
                    break;

                case 2:
                    PRIORITY.Bits.ARB_SEL2++;
                    break;

                case 3:
                    PRIORITY.Bits.ARB_SEL3++;
                    break;

                case 4:
                    PRIORITY.Bits.ARB_SEL4++;
                    break;

                case 5:
                    PRIORITY.Bits.ARB_SEL5++;
                    break;

                default:
                    ASSERT(FALSE);
                }

                // Update the interrupt controller status, both the Interrupt Pending and
                // InterruptOffset registers.
                INTPND.Word = InterruptBit;
                for (INTOFFSET=0, InterruptBit >>= 1; InterruptBit; INTOFFSET++, InterruptBit >>= 1)
                    ;

                // Notify the CPU of the pending interrupt
                CpuSetInterruptPending();
                return;
            }
        }
    }

    // We should never get here.  If PendingInterrupts is nonzero, then one of the
    // arbiters must be responsible for that pending interrupt and would have
    // raise it.
    ASSERT(FALSE);
}

void __fastcall IOInterruptController::SetSubInterruptPending(bool fMayClearSRCPND)
{
    SubInterruptCollection Pending;

    Pending.HalfWord = SUBSRCPND.HalfWord & ~INTSUBMSK.HalfWord;
    if (Pending.HalfWord & 0x0007) {
        SRCPND.Bits.INT_UART0 = 1;
    } else if (fMayClearSRCPND) {
        SRCPND.Bits.INT_UART0 = 0;
    }
    if (Pending.HalfWord & 0x0038) { 
        SRCPND.Bits.INT_UART1 = 1; 
    } else if (fMayClearSRCPND) {
        SRCPND.Bits.INT_UART1 = 0;
    }
    if (Pending.HalfWord & 0x01c0) {
        SRCPND.Bits.INT_UART2 = 1;
    } else if (fMayClearSRCPND) {
        SRCPND.Bits.INT_UART2 = 0;
    }
    if (Pending.Bits.INT_TC || Pending.Bits.INT_ADC) {
        SRCPND.Bits.INT_ADC = 1;
    } else if (fMayClearSRCPND) {
        SRCPND.Bits.INT_ADC = 0;
    }
    SetInterruptPending();
}


void __fastcall IOInterruptController::WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value)
{
    switch (IOAddress) {
    case 0: // SRCPND
        // For each '1' bit in Value, clear the corresponding bit in SRCPEND.  For each '0'
        // bit in Value, leave the corresponding bit in SRCPEND unchanged.
        SRCPND.Word &= ~Value;
        // If the keyboard queue is non-empty force the interrupt high
        if ( !SPI1.isQueueEmpty() )
            SRCPND.Bits.EINT1 = 1;
        break;

    case 4: // INTMOD
        if (Value != 0) TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        INTMOD.Word = Value;
        break;

    case 8: // INTMASK
        INTMASK.Word = Value;
        SetSubInterruptPending(false);  // raise any newly unmasked interrupts, including sub-interrupts
        break;

    case 0xc: // PRIORITY
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
                        // WinCE never changes this from the default 0x7f, which is:
                        //    Arbiter 0-6 priority rotate enable (ARB_MODEx==1)
                        //  Arbiter 0 REQ 1-2-3-4               (ARB_SELx==0)
                        //  Arbiter 1 REQ 0-1-2-3-4-5
                        //  Arbiter 2 REQ 0-1-2-3-4-5
                        //  Arbiter 3 REQ 0-1-2-3-4-5
                        //  Arbiter 4 REQ 0-1-2-3-4-5
                        //  Arbiter 5 REQ 1-2-3-4
                        //  Arbiter 6 REQ 0-1-2-3-4-5
        break;

    case 0x10: // INTPND
        INTPND.Word &= ~Value;
        if (INTPND.Word == 0) {
            INTOFFSET=0;
            CpuClearInterruptPending();

            // Merge in any level-triggered interrupts
            SRCPND.Word |= GPIO.GetLevelInterruptsPending() | PWMTimer.GetLevelInterruptsPending();
        }
        SetSubInterruptPending(false);
        break;

    case 0x14: // INTOFFSET
        INTOFFSET = Value;
        break;

    case 0x18: // SUBSRCPND
        // For each '1' bit in Value, clear the corresponding bit in SUBSRCPEND.  For each '0'
        // bit in Value, leave the corresponding bit in SUBSRCPEND unchanged.
        SUBSRCPND.HalfWord &= ~(unsigned __int16)Value;

        {
            unsigned __int32 LevelSubInterrupts = UART0.GetLevelSubInterruptsPending() | 
                                                  UART1.GetLevelSubInterruptsPending() | 
                                                  UART2.GetLevelSubInterruptsPending();
            if (LevelSubInterrupts) {
                SUBSRCPND.HalfWord |= LevelSubInterrupts;
                SetSubInterruptPending(false);
            }
        }

        break;

    case 0x1c: // INTSUBMSK
        INTSUBMSK.HalfWord = (unsigned __int16)Value;
        SetSubInterruptPending(false); // raise any unmasked sub interrupts
        break;

    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
    }
}


unsigned __int32 __fastcall IOInterruptController::ReadWord(unsigned __int32 IOAddress)
{
    switch (IOAddress) {
    case 0: // SRCPEND
        return SRCPND.Word;

    case 4: // INTMOD
        return INTMOD.Word;

    case 8: // INTMASK
        return INTMASK.Word;

    case 0xc: // PRIORITY
        return 0x7f; // WinCE never changes this from the default

    case 0x10: // INTPND
        return INTPND.Word;

    case 0x14: // INTOFFSET
        return INTOFFSET;

    case 0x18:
        return SUBSRCPND.HalfWord;

    case 0x1c:
        return INTSUBMSK.HalfWord;

    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        return 0;
    }
}

void __fastcall IOInterruptController::RaiseInterrupt(InterruptSource Source)
{
    SRCPND.Word |= Source;
    SetInterruptPending();
}

void __fastcall IOInterruptController::RaiseInterrupt(InterruptSubSource Source)
{
    SUBSRCPND.HalfWord |= Source;
    SetSubInterruptPending(true);
}

IOPWMTimer::IOPWMTimer()
{
    MultimediaTimerPeriod=0;
}

IOPWMTimer::~IOPWMTimer()
{
    if (MultimediaTimerPeriod) {
        timeEndPeriod(MultimediaTimerPeriod);
    }
}

bool __fastcall IOPWMTimer::PowerOn(void)
{
    DWORD dwThreadId;

        // Shorten the HAL's timer resolution down as much as possible, preferrably to
        // 1ms, but any value smaller than 10 is acceptable.
        for (UINT i=1;i<11;++i) {
            MMRESULT Result = timeBeginPeriod(i);
            if (Result == TIMERR_NOERROR) {
                MultimediaTimerPeriod = i;
                break;
            }
        }
    hTimer2 = CreateWaitableTimer(NULL, FALSE, NULL); // create anonymous sync timer
    hTimer3 = CreateWaitableTimer(NULL, FALSE, NULL); // create anonymous sync timer
    hTimer4 = CreateWaitableTimer(NULL, FALSE, NULL); // create anonymous sync timer
    hTimerThread = CreateThread(NULL, 0, IOPWMTimer::TimerThreadProc, NULL, 0, &dwThreadId);
    if (hTimer2 == INVALID_HANDLE_VALUE || hTimer3 == INVALID_HANDLE_VALUE ||
        hTimer4 == INVALID_HANDLE_VALUE || hTimerThread == INVALID_HANDLE_VALUE) {
        ShowDialog(ID_MESSAGE_INTERNAL_ERROR);
        return false;
    }
    // The timer interrupt thread must take priority over the main emulator thread.
    // Otherwise, timer interrupt latency is too long, causing all manner of timing
    // problems, from keyboard repeat to menu painting.
    if (!SetThreadPriority(hTimerThread, THREAD_PRIORITY_HIGHEST)) {
        LogPrint((output, "Failed to adjust TimerThread priority\n"));
        return false;
    }
    // Get the timer frequency and convert from ticks/second to ticks/millisecond.
    QueryPerformanceFrequency(&PerformanceCounterFrequency);
    PerformanceCounterFrequency.QuadPart/=1000;
    return true;
}

void IOPWMTimer::ReloadTimer(bool TimerXAutoReload, HANDLE hTimerX,
                                     LARGE_INTEGER * TimerXStartTime, 
                                     LARGE_INTEGER * TimerXPeriod)
{
    if (TimerXAutoReload) {
        // Update the time that the timer was started.
        QueryPerformanceCounter(TimerXStartTime);
        // Restart the timer
        if (SetWaitableTimer(hTimerX, TimerXPeriod, 0, NULL, 0, FALSE) == FALSE) {
            ASSERT(FALSE);
        }
    }
}

DWORD WINAPI IOPWMTimer::TimerThreadProc(LPVOID lpvThreadParam)
{
    HANDLE Handles[3];

    Handles[0] = PWMTimer.hTimer2;
    Handles[1] = PWMTimer.hTimer3;
    Handles[2] = PWMTimer.hTimer4;

    while (1) {
        DWORD dw;

        dw = WaitForMultipleObjects(ARRAY_SIZE(Handles), Handles, FALSE, INFINITE);
        EnterCriticalSection(&IOLock);
        switch (dw) {
        case WAIT_OBJECT_0+0:
            PWMTimer.Timer2InterruptPending=true;
            ReloadTimer(PWMTimer.TCON.Bits.Timer2AutoReload, PWMTimer.hTimer2, 
                        &PWMTimer.Timer2StartTime, &PWMTimer.Timer2Period);
            InterruptController.RaiseInterrupt(IOInterruptController::SourceINT_TIMER2);
            break;
        case WAIT_OBJECT_0+1:
            PWMTimer.Timer3InterruptPending=true;
            ReloadTimer(PWMTimer.TCON.Bits.Timer3AutoReload, PWMTimer.hTimer3, 
                        &PWMTimer.Timer3StartTime, &PWMTimer.Timer3Period);
            InterruptController.RaiseInterrupt(IOInterruptController::SourceINT_TIMER3);
            break;
        case WAIT_OBJECT_0+2:
            PWMTimer.Timer4InterruptPending=true;
            ReloadTimer(PWMTimer.TCON.Bits.Timer4AutoReload, PWMTimer.hTimer4, 
                        &PWMTimer.Timer4StartTime, &PWMTimer.Timer4Period);
            InterruptController.RaiseInterrupt(IOInterruptController::SourceINT_TIMER4);
            break;
        default:
            ASSERT(FALSE);
        }
        LeaveCriticalSection(&IOLock);
    }
}

void __fastcall IOPWMTimer::SaveState(StateFiler& filer) const
{
    filer.Write('PWMT');
    filer.Write(TCFG0.Word);
    filer.Write(TCFG1.Word);
    filer.Write(TCON.Word);
    filer.Write(TCNTB0);
    filer.Write(TCMPB0);
    filer.Write(TCNTB1);
    filer.Write(TCMPB1);
    filer.Write(TCNTB2);
    filer.Write(TCMPB2);
    filer.Write(TCNTB3);
    filer.Write(TCMPB3);
    filer.Write(TCNTB4);
    filer.Write(Timer4StartTime);
}

void __fastcall IOPWMTimer::RestoreState(StateFiler& filer)
{
    filer.Verify('PWMT');
    filer.Read(TCFG0.Word);
    filer.Read(TCFG1.Word);
    filer.Read(TCON.Word);
    filer.Read(TCNTB0);
    filer.Read(TCMPB0);
    filer.Read(TCNTB1);
    filer.Read(TCMPB1);
    filer.Read(TCNTB2);
    filer.Read(TCMPB2);
    filer.Read(TCNTB3);
    filer.Read(TCMPB3);
    filer.Read(TCNTB4);
    filer.Read(Timer4StartTime);
}

// Compute a DueTime for SetWaitableTimer() based on the PWM timer's
// prescale, MUX, and count buffer.
__int64 IOPWMTimer::ComputeDueTime(unsigned __int8 Prescale, int MUXInput, unsigned __int32 CountBuffer)
{
    double Scale;
    double Divisor;

    // OEMCount1ms == OEM_COUNT_1MS = ((((203 * 1000 * 1000) / 4) / 200 / 4) / 1000) == 63
    // so there are 63.4375 timer ticks per millisecond with Prescaler1==200 and MUX4==1.

    Scale = 1.0;
    if (Prescale) {
        Scale *= (double)Prescale;
    }
    switch (MUXInput) {
    case 0:
        Scale *= 2.0;
        break;
    case 1:
        Scale *= 4.0;
        break;
    case 2:
        Scale *= 8.0;
        break;
    case 3:
        Scale *= 16.0;
        break;
    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE); /* MuxInput is 4 bits wide */
        break;
    }

    Divisor = ((203 * 1000 * 1000) / 4) / Scale / 1000; // 63.4375 ticks/ms with Prescale at 200 and MUX at 1/4
    Divisor /= 10000.0; // scale to ticks / 100ns

    // Negative means relative to now, as opposed to an absolute time.
    return -(__int64)((double)CountBuffer/Divisor);
}


void IOPWMTimer::ActivateTimer(HANDLE hTimerX, unsigned __int32 TCNTBX, 
                               int MUXInput,
                               LARGE_INTEGER * TimerXStartTime,
                               LARGE_INTEGER * TimerXPeriod)
{
    // TCNTBx contains the countdown time until the interrupt should be triggered
    LARGE_INTEGER DueTime;

    DueTime.QuadPart = ComputeDueTime(TCFG0.Bits.Prescaler1, MUXInput, TCNTBX);
    // Work around a windows bug where requests close to 1ms - generate a 2ms timer
    DueTime.QuadPart = ( DueTime.QuadPart == -10000 ? -9750: DueTime.QuadPart);
    TimerXPeriod->QuadPart = DueTime.QuadPart;

    // Record the time that the timer was started.
    QueryPerformanceCounter(TimerXStartTime);

    // Wake up the already-waiting thread when the right time arrives
    if (SetWaitableTimer(hTimerX, &DueTime, 0, NULL, 0, FALSE) == FALSE) {
        ASSERT(FALSE);
    }
}

void __fastcall IOPWMTimer::WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value)
{
    switch (IOAddress) {
    case 0:        // TCFG0
        TCFG0.Word = Value;
        break;

    case 4:        // TFCF1
        TCFG1.Word = Value;
        break;

    case 8:        // TCON
        {
            TCONRegister NewTCON;

            NewTCON.Word = Value;

            // only timers 3 and 4 are supported
            if (TCON.Bits.Timer0StartStop != 0 || TCON.Bits.Timer1StartStop != 0 )
                TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);

            if (NewTCON.Bits.Timer4StartStop && TCON.Bits.Timer4StartStop == 0) {
                // TCNTB4 contains the countdown time until the interrupt should be triggered
                ActivateTimer( hTimer4, TCNTB4, 
                               TCFG1.Bits.MUX4, &Timer4StartTime, &Timer4Period);
            } else if (NewTCON.Bits.Timer4StartStop == 0 && TCON.Bits.Timer4StartStop) {
                Timer4InterruptPending=false;
                CancelWaitableTimer(hTimer4);
            }

            if (NewTCON.Bits.Timer3StartStop && TCON.Bits.Timer3StartStop == 0) {
                // TCNTB3 contains the countdown time until the interrupt should be triggered
                ActivateTimer( hTimer3, TCNTB3, 
                               TCFG1.Bits.MUX3, &Timer3StartTime, &Timer3Period);
            } else if (NewTCON.Bits.Timer3StartStop == 0 && TCON.Bits.Timer3StartStop) {
                Timer3InterruptPending=false;
                CancelWaitableTimer(hTimer3);
            }

            if (NewTCON.Bits.Timer2StartStop && TCON.Bits.Timer2StartStop == 0) {
                // TCNTB2 contains the countdown time until the interrupt should be triggered
                ActivateTimer( hTimer2, TCNTB2, 
                               TCFG1.Bits.MUX2, &Timer2StartTime, &Timer2Period);
            } else if (NewTCON.Bits.Timer2StartStop == 0 && TCON.Bits.Timer2StartStop) {
                Timer2InterruptPending=false;
                CancelWaitableTimer(hTimer2);
            }

            TCON.Word = NewTCON.Word;
        }
        break;

    case 0x0c:    // TCNTB0
        TCNTB0 = Value;
        break;

    case 0x10:  // TCMPB0
        TCMPB0 = Value;
        break;

    case 0x14:  // TCNTO0
        break; // register is readonly

    case 0x18:    // TCNTB1
        TCNTB1 = Value;
        break;

    case 0x1C:  // TCMPB1
        TCMPB1 = Value;
        break;

    case 0x20:  // TCNTO1
        break;  // TCNTO1 is readonly

    case 0x24:  // TCNTB2
        TCNTB2 = Value;
        break;

    case 0x28:  // TCMPB2
        TCMPB2 = Value;
        break;

    case 0x2c:  // TCNTO2
        break;  // TCNTO2 is readonly

    case 0x30:  // TCNTB3
        TCNTB3 = Value;
        break;

    case 0x34:  // TCMPB3
        TCMPB3 = Value;
        break;

    case 0x38:  // TCNTO3
        break;  // TCNTO3 is readonly

    case 0x3c:  // TCNTB4 - this is the OS timer tick, set to decimal 63 or 15 by Wince, depending on the clock divider selected in the BSP
        TCNTB4 = Value;
        break;

    case 0x40:  // TCNTO4
        break;  // TCNTO4 is readonly

    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
    }
}

unsigned __int32 IOPWMTimer::GetLevelInterruptsPending(void)
{
    unsigned __int32 ret=0;

    // If CPUClearSysTimerIRQ() is called, and there is a
    // timer interrupt pending, the routine clears the interrupt by writing
    // to SRCPND/INTPND.  However, nothing gets around to resetting the timer, 
    // which is only done by having OEMInterruptHandler() receive a timer4 interrupt.
    // My guess is that when CPUClearSysTimerIRQ() clears the interrupt,
    // the PWM Timer device continues to assert the TIMER4 interrupt, so that the
    // act of clearing the interrupt just re-asserts TIMER4.  ie. the PWMTimer
    // uses level-triggered interrupts, not edge-triggered.
    if (Timer2InterruptPending && TCON.Bits.Timer2AutoReload == 0) {
        ret |= IOInterruptController::SourceINT_TIMER2;
    }
    if (Timer3InterruptPending && TCON.Bits.Timer3AutoReload == 0) {
        ret |= IOInterruptController::SourceINT_TIMER3;
    }
    if (Timer4InterruptPending && TCON.Bits.Timer4AutoReload == 0) {
        ret |= IOInterruptController::SourceINT_TIMER4;
    }
    return ret;
}

unsigned __int32 IOPWMTimer::CalcObservationReg(LARGE_INTEGER * TimerXStartTime,
                                                LARGE_INTEGER * TimerXPeriod,
                                                __int32 TCNTBx,
                                                bool TimerXAutoReload,
                                                bool TimerXIntPending)
{
    LARGE_INTEGER Now;
    __int64 TicksElapsed;

    // If the interrupt has not been serviced returning the countdown for the next
    // timer from the observation register will make it look like time is going
    // backwards. We latch at zero countdown until the timer interrupt is serviced
    if ( TimerXAutoReload && TimerXIntPending )
        return 0;

    QueryPerformanceCounter(&Now);
    if (Now.QuadPart < TimerXStartTime->QuadPart) {
        // Time can appear to run backwards on multiprocessor machines
        // with buggy BIOS or HAL.
        TicksElapsed=0;
    } else {
        TicksElapsed = Now.QuadPart - TimerXStartTime->QuadPart;
        // Note that if WinCE has exceeded its 1ms timeslice, the
        // return value from this function may be less than zero.
        // The SMDK2410 timer.c's PerfCountSinceTick() is set up
        // to handle this case gracefully in CE4.2.  Otherwise, TicksElapsed
        // could be capped to min(TicksElapsed, PerformanceCounterFrequency).
    }
    // TicksElapsed is now 0...(PerformanceCounterFrequency/1000).  Convert to
    // 0...TCNTBx ticks. TimerXPeriod is in 100ns, PerformanceCounterFrequency is in 
    // counter per 1 ms - we need counts per TimerXPeriod, so we convert the timer
    // period to ms.
    TicksElapsed = (TicksElapsed*TCNTBx*10000) 
                   / (PerformanceCounterFrequency.QuadPart*(-TimerXPeriod->QuadPart));

    // And finally, convert to TCNTBx...0 ticks.
    TicksElapsed = (int)(TCNTBx-TicksElapsed);

    // We never want to exceed the timeslice if were in are in the auto reload
    // mode. However because of the overhead on the signaling of the event and
    // waking up of the timer thread, timer thread getting the IOLock - we
    // end up being off up to 1/2000 of a second.
    return (unsigned int)( TicksElapsed < 0 && TimerXAutoReload ? 
                           0 : TicksElapsed);
}

unsigned __int32 __fastcall IOPWMTimer::ReadWord(unsigned __int32 IOAddress)
{
    switch (IOAddress) {
    case 0:        // TCFG0
        return TCFG0.Word;

    case 4:        // TFCF1
        return TCFG1.Word;

    case 8:        // TCON
        return TCON.Word;

    case 0x0c:    // TCNTB0
        return TCNTB0;

    case 0x10:  // TCMPB0
        return TCMPB0;

    case 0x14:  // TCNTO0
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        return 0;

    case 0x18:    // TCNTB1
        return TCNTB1;

    case 0x1C:  // TCMPB1
        return TCMPB1;

    case 0x20:  // TCNTO1
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        return 0;

    case 0x24:  // TCNTB2
        return TCNTB2;

    case 0x28:  // TCMPB2
        return TCMPB2;

    case 0x2c:  // TCNTO2
        return CalcObservationReg(&Timer2StartTime, &Timer2Period, TCNTB2, 
                                  TCON.Bits.Timer2AutoReload,
                                  InterruptController.IsPending(IOInterruptController::SourceINT_TIMER2));

    case 0x30:  // TCNTB3
        return TCNTB3;

    case 0x34:  // TCMPB3
        return TCMPB3;

    case 0x38:  // TCNTO3
        return CalcObservationReg(&Timer3StartTime, &Timer3Period, TCNTB3, TCON.Bits.Timer3AutoReload,
                                  InterruptController.IsPending(IOInterruptController::SourceINT_TIMER3));
    case 0x3c:  // TCNTB4
        return TCNTB4;

    case 0x40:  // TCNTO4
        return CalcObservationReg(&Timer4StartTime, &Timer4Period, TCNTB4, TCON.Bits.Timer4AutoReload,
                                  InterruptController.IsPending(IOInterruptController::SourceINT_TIMER4));

    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        return 0;
    }
}

bool __fastcall IOGPIO::Reset(void)
{
    // Clear out the reason for reset so that CE doesn't think we are resuming
    GSTATUS2&=-3;
    return true;
}

void __fastcall IOGPIO::SaveState(StateFiler& filer) const
{
    filer.Write('GPIO');
    filer.Write(GPACON);
    filer.Write(GPADAT);
    filer.Write(GPBCON);
    filer.Write(GPBDAT);
    filer.Write(GPBUP);
    filer.Write(GPCCON);
    filer.Write(GPCDAT);
    filer.Write(GPCUP);
    filer.Write(GPDCON);
    filer.Write(GPDDAT);
    filer.Write(GPDUP);
    filer.Write(GPECON);
    filer.Write(GPEDAT);
    filer.Write(GPEUP);
    filer.Write(GPFCON);
    filer.Write(GPFDAT);
    filer.Write(GPFUP);
    filer.Write(GPGCON);
    filer.Write(GPGDAT);
    filer.Write(GPGUP);
    filer.Write(GPHCON);
    filer.Write(GPHDAT);
    filer.Write(GPHUP);
    filer.Write(MISCCR);
    filer.Write(DCLKCON);
    filer.Write(EINTFLT2);
    filer.Write(EINTFLT3);
    filer.Write(GSTATUS2);
    filer.Write(GSTATUS3);
    filer.Write(EXTINT0);
    filer.Write(EXTINT1);
    filer.Write(EXTINT2);
    filer.Write(EINTMASK);
    filer.Write(EINTPEND);
}

void __fastcall IOGPIO::RestoreState(StateFiler& filer)
{
    filer.Verify('GPIO');
    filer.Read(GPACON);
    filer.Read(GPADAT);
    filer.Read(GPBCON);
    filer.Read(GPBDAT);
    filer.Read(GPBUP);
    filer.Read(GPCCON);
    filer.Read(GPCDAT);
    filer.Read(GPCUP);
    filer.Read(GPDCON);
    filer.Read(GPDDAT);
    filer.Read(GPDUP);
    filer.Read(GPECON);
    filer.Read(GPEDAT);
    filer.Read(GPEUP);
    filer.Read(GPFCON);
    filer.Read(GPFDAT);
    filer.Read(GPFUP);
    filer.Read(GPGCON);
    filer.Read(GPGDAT);
    filer.Read(GPGUP);
    filer.Read(GPHCON);
    filer.Read(GPHDAT);
    filer.Read(GPHUP);
    filer.Read(MISCCR);
    filer.Read(DCLKCON);
    filer.Read(EINTFLT2);
    filer.Read(EINTFLT3);
    filer.Read(GSTATUS2);
    filer.Read(GSTATUS3);
    filer.Read(EXTINT0);
    filer.Read(EXTINT1);
    filer.Read(EXTINT2);
    filer.Read(EINTMASK);
    filer.Read(EINTPEND);
    if (filer.ForceResume())
        GSTATUS2|=2;
    else
        if (filer.ForceReboot())
            GSTATUS2&=-3;
}

void __fastcall IOGPIO::WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value)
{
    switch (IOAddress) {
    case 0x88:
        EXTINT0 = Value;
        break;

    case 0x8c:
        EXTINT1 = Value;
        break;

    case 0x90:
        EXTINT2 = Value;
        break;

    case 0xa4:
        EINTMASK = Value;
        if (EINTMASK & ~EINTPEND) { // an interrupt has been unmasked
            InterruptController.RaiseInterrupt(InterruptController.SourceEINT8_23);
        }
        break;

    case 0xa8:
        EINTPEND &= ~Value;
        break;

    case 0xb8:
        GSTATUS3 = Value;
        break;

    default:
        WriteHalf(IOAddress, (unsigned __int16)Value);
        break;
    }
}

void __fastcall IOGPIO::WriteHalf(unsigned __int32 IOAddress, unsigned __int16 Value)
{
    /* This table lists known GPIO interrupt mappings:
        EINT0    - power button        - PBT_EnableInterrupt()
        EINT1   - keyboard            - Ps2Keybd::KeybdPowerOn()
        EINT2   - reset             - OEMInterruptHandler()
        EINT3   - PCMCIA            - PCMCIA_Init()
        EINT4   - unused
        EINT5   - unused
        EINT6   - unused
        EINT7   - unused
        EINT8   - PCMCIA PD6720        - PCMCIA_Init()
        EINT9   - ethernet CS8900   - OEMInterruptHandler()
        EINT10  - DeviceEmulator DMA
        EINT11  - DeviceEmulator EmulServ
    */
    switch (IOAddress) {

    case 0x00:
         GPACON = Value; break;
    case 0x04:
         GPADAT = Value; break;

    case 0x10: 
         GPBCON = Value; break;
    case 0x14:
         GPBDAT = Value; break;
    case 0x18:
         GPBUP = Value; break;

    case 0x20:
         GPCCON = Value; break;
    case 0x24:
         GPCDAT = Value; break;
    case 0x28:
         GPCUP = Value; break;

    case 0x30:
         GPDCON = Value; break;
    case 0x34:
         GPDDAT = Value; break;
    case 0x38:
         GPDUP = Value; break;

    case 0x40:
         GPECON = Value; break;
    case 0x44:
         GPEDAT = Value; break;
    case 0x48:
         GPEUP = Value; break;

    case 0x50:
         GPFCON = Value; break;
    case 0x54:
         GPFDATReadCount=0;
         if (PowerButtonWasPressed) {
              Value &= ~1; // clear bit 0
         } else {
             Value |= 1;
         }
         GPFDAT = (unsigned __int8)Value; break;
    case 0x58:
         GPFUP = (unsigned __int8)Value; break;

    case 0x60:
         GPGCON = Value; break;
    case 0x64:
         GPGDAT = Value; break;
    case 0x68:
         GPGUP = Value; break;

    case 0x70:
         GPHCON = Value; break;
    case 0x74:
        // 1 << 6 is UART0's DTR bit
        if ( (GPHDAT ^ Value) & (1<<6)) {
            UART0.ChangeDTR(Value & (1<<6));
        }
         GPHDAT = Value; break;
    case 0x78:
         GPHUP = Value; break;

    case 0x80:
         MISCCR = Value; break;

    case 0x84:
         DCLKCON = Value; break;

    case 0x94:
         break; // EINTFLT0 - reserved
    case 0x98:    
         break; // EINTFLT1 - reserved
    case 0x9c:
         EINTFLT2 = Value; break;
    case 0xa0:
         EINTFLT3 = Value; break;

    case 0xb4:
         GSTATUS2 = (unsigned __int8)Value; break;
    case 0xb8:
         GSTATUS3 = Value; break;

    default:
        LogPrint((output, "Unsupported GPIO register 0x%x needs to be implemented\n", IOAddress));
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
    }
}

unsigned __int32 __fastcall IOGPIO::ReadWord(unsigned __int32 IOAddress)
{
    switch (IOAddress) {
    case 0x88:
        return EXTINT0;

    case 0x8c:
        return EXTINT1;

    case 0x90:
        return EXTINT2;

    case 0xa4:
        return EINTMASK;

    case 0xa8:
        return EINTPEND;

    case 0xb8:
        return GSTATUS3;

    default:
        return ReadHalf(IOAddress);
    }
}

unsigned __int16 __fastcall IOGPIO::ReadHalf(unsigned __int32 IOAddress)
{
    switch (IOAddress) {

    case 0x00:
        return (unsigned __int16)GPACON;
    case 0x04:
        return (unsigned __int16)GPADAT;

    case 0x10: 
        return (unsigned __int16)GPBCON;
    case 0x14:
        return GPBDAT;
    case 0x18:
        return GPBUP;

    case 0x20:
        return (unsigned __int16)GPCCON;
    case 0x24:
        return GPCDAT;
    case 0x28:
        return GPCUP;

    case 0x30:
        return (unsigned __int16)GPDCON;
    case 0x34:
        return GPDDAT;
    case 0x38:
        return GPDUP;

    case 0x40:
        return (unsigned __int16)GPECON;
    case 0x44:
        return GPEDAT;
    case 0x48:
        return GPEUP;

    case 0x50:        // GPFCON
        return GPFCON;
    case 0x54:        // GPFDAT
        GPFDATReadCount++;
        if (GPFDATReadCount == 2 && PowerButtonWasPressed) {
            // Two consective reads from GPFDAT have completed while the power
            // button was pressed.  One of those will be from pwrbtn2410.c
            // and the other from OEMWriteDebug(), which does a read/modify/write
            // on each timer interrupt.  The goal is to allow pwrbtn2410.c to
            // read the button as pressed once, Sleep(200), then read the button
            // as released, even though timer interrupts are firing during the
            // Sleep() call.
            PowerButtonWasPressed=false;
            GPFDAT |= 1;
        }
        return GPFDAT;
    case 0x58:        // GPFUP
        return GPFUP;

    case 0x60:
        return (unsigned __int16)GPGCON;
    case 0x64:
        return GPGDAT;
    case 0x68:
        return GPGUP;

    case 0x70:
        return (unsigned __int16)GPHCON;
    case 0x74:
        // 1 << 7 is UART0's DSR bit.  See ser2410_hw.c's S2410_SetSerialIOP() for more info.
        // The line is low-active, so if the DSR is set, the bit in GPHDAT must be cleared.
        if (UART0.GetDSR()) {
            GPHDAT &= ~(1<<7);
        } else {
            GPHDAT |= (1<<7);
        }
        return GPHDAT;
    case 0x78:
        return GPHUP;

    case 0x80:
        return (unsigned __int16)MISCCR;

    case 0x84:
        return (unsigned __int16)DCLKCON;

    case 0x94:    
        return 0; // EINTFLT0 - reserved
    case 0x98:    
        return 0; // EINTFLT1 - reserved
    case 0x9c:
        return (unsigned __int16)EINTFLT2;
    case 0xa0:
        return (unsigned __int16)EINTFLT3;

    case 0xb4:
        return GSTATUS2;
    case 0xb8:
        return (unsigned __int16)GSTATUS3;

    default:
        LogPrint((output, "Unsupported GPIO register 0x%x needs to be implemented\n", IOAddress));
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        return 0;
    }
}

void IOGPIO::PowerButtonPressed(void)
{
    EnterCriticalSection(&IOLock);
    PowerButtonWasPressed=true;
    GPFDATReadCount=0;
    // clear the low bit in GPFDAT to indicate that the button was pressed
    GPFDAT &= ~1;
    if ((GPFCON & 3) == 2) {
        // Power button was pressed and GPFCON is set to raise EINT0
        InterruptController.RaiseInterrupt(InterruptController.SourceEINT0);
    }
    LeaveCriticalSection(&IOLock);
}

void IOGPIO::RaiseInterrupt(unsigned int InterruptNum)
{
    unsigned int InterruptBit;
    unsigned __int32 InterruptType;

    ASSERT(InterruptNum > 3 && InterruptNum < 24); // EINT0-3 are raised directly through the InterruptController

    InterruptBit = 1 << InterruptNum;
    if (InterruptNum < 8) {
        InterruptType = (EXTINT0 >> (InterruptNum*4)) & 0x7;
    } else if (InterruptNum < 16) {
        InterruptType = (EXTINT1 >> ((InterruptNum-8)*4)) & 0x7;
    } else {
        InterruptType = (EXTINT2 >> ((InterruptNum-16)&4)) & 0x7;
    }

    switch (InterruptType) {
    case 0:        // 000 - Low level
        // unsupported by the emulator, but this code-path is hit with CS8900 as the kernel debugger
        break;

    case 2:        // 010 - falling edge triggered
    case 3:        // 011 - falling edge triggered
        break;    // nothing needs to be done

    case 1:     // 001 - High level triggered
        EINTLevel |= InterruptBit;
        // fall through into rising/both edge triggered
    case 4:     // 100 - Rising edge triggered
    case 5:     // 101 - Rising edge triggered
    case 6:     // 110 - Both edge triggered
    case 7:     // 111 - Both edge triggered
        EINTPEND |= InterruptBit;
        if ((EINTMASK & InterruptBit) == 0) {
            // Interrupt is unmasked.  Report it now
            if (InterruptNum < 8) {
                InterruptController.RaiseInterrupt(InterruptController.SourceEINT4_7);
            } else {
                InterruptController.RaiseInterrupt(InterruptController.SourceEINT8_23);
            }
        }
        break;

    default:    // error - InterruptType was ANDed with 7 earlier 
        ASSERT(FALSE);
        break;
    }
}

void IOGPIO::ClearInterrupt(unsigned int InterruptNum)
{
    unsigned int InterruptBit;
    unsigned __int32 InterruptType;

    ASSERT(InterruptNum > 3 && InterruptNum < 24); // EINT0-3 are raised directly through the InterruptController

    InterruptBit = 1 << InterruptNum;
    if (InterruptNum < 8) {
        InterruptType = (EXTINT0 >> (InterruptNum*4)) & 0x7;
    } else if (InterruptNum < 16) {
        InterruptType = (EXTINT1 >> ((InterruptNum-8)*4)) & 0x7;
    } else {
        InterruptType = (EXTINT2 >> ((InterruptNum-16)&4)) & 0x7;
    }

    switch (InterruptType) {
    case 0:        // 000 - Low level
        // unsupported by the emulator, but this code-path is hit with CS8900 as the kernel debugger
        break;

    case 1:     // 001 - High level triggered
        EINTLevel &= ~InterruptBit;
        break;

    case 4:     // 100 - Rising edge triggered
    case 5:     // 101 - Rising edge triggered
        break;    // nothing needs to be done

        // fall through into rising/both edge triggered
    case 2:        // 010 - falling edge triggered
    case 3:        // 011 - falling edge triggered
    case 6:     // 110 - Both edge triggered
    case 7:     // 111 - Both edge triggered
        EINTPEND |= InterruptBit;
        if ((EINTMASK & InterruptBit) == 0) {
            // Interrupt is unmasked.  Report it now
            if (InterruptNum < 8) {
                InterruptController.RaiseInterrupt(InterruptController.SourceEINT4_7);
            } else {
                InterruptController.RaiseInterrupt(InterruptController.SourceEINT8_23);
            }
        }
        break;

    default:    // error - InterruptType was ANDed with 7 earlier 
        ASSERT(FALSE);
        break;
    }
}

unsigned __int32 IOGPIO::GetLevelInterruptsPending(void)
{
    unsigned __int32 Ret;
    unsigned __int32 Pending = EINTLevel & ~EINTMASK;

    Ret = 0;
    ASSERT ((Pending & 0xf) == 0); // EINT0...3 are controlled directly via the InterruptController, not via GPIO
    EINTPEND |= Pending;
    if (Pending & 0xf0) {
        Ret = InterruptController.SourceEINT4_7;
    }
    if (Pending & 0xfff00) {
        Ret |= InterruptController.SourceEINT8_23;
    }
    return Ret;
}


IONANDFlashController::IONANDFlashController()
:m_Flash(NULL),m_SavedFileName(NULL),m_SaveCount(0)
{
}
IONANDFlashController::~IONANDFlashController()
{
    if (m_Flash)
        delete [] m_Flash;
    if (m_SavedFileName)
        delete [] m_SavedFileName;
}

DWORD WINAPI IONANDFlashController::NANDFlashSaveStatic(LPVOID lpvThreadParam)
{
    IONANDFlashController *pThis = (IONANDFlashController *)lpvThreadParam;

    ASSERT( pThis->m_SaveCount == 1 );

    StateFiler filer;
    filer.setStatus(true);

    // If this routine is called the filename should not be NULL
    ASSERT(pThis->m_SavedFileName != NULL );
    if (pThis->m_SavedFileName == NULL )
        goto Exit;

    LCDController.DisableUIMenuItem(ID_FLASH_SAVE);

    if ( !pThis->SaveStateToFile(filer, pThis->m_SavedFileName) )
        ShowDialog(ID_MESSAGE_FAILED_SAVE_FLASH, pThis->m_SavedFileName);
    else
        ShowDialog(ID_MESSAGE_SUCCESS_SAVE_FLASH, pThis->m_SavedFileName);

    LCDController.EnableUIMenuItem(ID_FLASH_SAVE);
Exit:
    InterlockedDecrement((LONG*)&pThis->m_SaveCount);
    return 0;
}

bool __fastcall IONANDFlashController::PowerOn()
{
    // Check if we already restored flash
    if ( m_Flash != NULL )
    {
        ASSERT( m_NumberBlocks == NAND_BLOCK_COUNT && m_SectorsPerBlock == NAND_BLOCK_SIZE &&
                m_BytesPerSector == NAND_SECTOR_SIZE );
        return true;
    }

    if ( Configuration.getFlashEnabled() && Configuration.isFlashStateFileSpecified())
    {
        StateFiler filer;
        filer.setStatus(true);
        bool status = NANDFlashController.RestoreStateFromFile(filer, Configuration.getFlashStateFile());
        if (status)
            return true; 
    }

    if ( Configuration.getFlashEnabled() ) 
    {
        // Attempt to initialize the flash array from the resource file
        if (NANDFlashController.InitializeFlashFromResource())
            return true;

        // Default flash configuration
        m_NumberBlocks    = NAND_BLOCK_COUNT;
        m_SectorsPerBlock = NAND_BLOCK_SIZE;
        m_BytesPerSector  = NAND_SECTOR_SIZE;

        // Create the backing store for the flash
        m_Flash = new unsigned __int8[m_NumberBlocks*m_SectorsPerBlock*m_BytesPerSector];

        if ( m_Flash == NULL )
        {
            ShowDialog(ID_MESSAGE_RESOURCE_EXHAUSTED);
            return false;
        }

        // Format the sector info for all sectors - the sectors are reported as good but in unknown state
        for (unsigned int sector = 0; sector < m_NumberBlocks*m_SectorsPerBlock; sector++)
        {
             int * sector_info = (int *)&m_Flash[sector*m_BytesPerSector + NAND_CONFIG_ADDR];
             *sector_info = 0xffffffff;
             sector_info++;
             *sector_info = 0xffffffff;
        }
    }

    return true;
}
bool __fastcall IONANDFlashController::SaveStateToFile(StateFiler& filer, __in_z const wchar_t * flashFileName) const
{
    bool status = false;
    HANDLE hFile = INVALID_HANDLE_VALUE;

    // If there no flash return failure
    if (!m_Flash) goto DoneFlashSave;

    hFile = CreateFile(flashFileName,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if (hFile==INVALID_HANDLE_VALUE)
        return false;

    unsigned __int32 versionAndFlashType= 0;
    if (!filer.Write(hFile,&versionAndFlashType,sizeof(__int32)) ) goto DoneFlashSave;

    if (!filer.Write(hFile,&m_NumberBlocks,sizeof(__int32))) goto DoneFlashSave;

    unsigned __int32 BytesPerBlock = m_BytesPerSector*m_SectorsPerBlock;
    if (!filer.Write(hFile,&BytesPerBlock,sizeof(__int32))) goto DoneFlashSave;

    unsigned __int32 SecPerBlockAndBytesPerSec = (m_BytesPerSector << 16) + (m_SectorsPerBlock & 0xffff);
    if (!filer.Write(hFile,&SecPerBlockAndBytesPerSec,sizeof(__int32))) goto DoneFlashSave;

    if (!filer.LZWrite(m_Flash, m_NumberBlocks*m_SectorsPerBlock*m_BytesPerSector, hFile)) goto DoneFlashSave;

    status = true;

DoneFlashSave:
    if ( hFile!=INVALID_HANDLE_VALUE )
        CloseHandle(hFile);

    return status;
}

bool __fastcall IONANDFlashController::RestoreStateFromFile(StateFiler& filer, __in_z const wchar_t * flashFileName)
{
    bool status = false;
    HANDLE hFile = INVALID_HANDLE_VALUE;

    // Delete the current backing store as it may the wrong size
    if ( m_Flash )
        delete [] m_Flash;
    m_Flash = NULL;

    // Try to open the file containing the flash data
    hFile = 
        CreateFile(flashFileName,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if (hFile==INVALID_HANDLE_VALUE)
        return false;

    unsigned __int32 versionAndFlashType= 0;
    if (!filer.Read(hFile,&versionAndFlashType,sizeof(__int32)) ) goto DoneFlashRestore;

    // Version = 0, FlashType = 0
    if ( versionAndFlashType != 0 ) goto DoneFlashRestore;

    if (!filer.Read(hFile,&m_NumberBlocks,sizeof(__int32))) goto DoneFlashRestore;
    // Version = 0, FlashType = 0
    if ( m_NumberBlocks != NAND_BLOCK_COUNT ) goto DoneFlashRestore;

    unsigned __int32 BytesPerBlock = 0;
    if (!filer.Read(hFile,&BytesPerBlock,sizeof(__int32))) goto DoneFlashRestore;

    unsigned __int32 SecPerBlockAndBytesPerSec = 0;
    if (!filer.Read(hFile,&SecPerBlockAndBytesPerSec,sizeof(__int32))) goto DoneFlashRestore;
    m_BytesPerSector  = SecPerBlockAndBytesPerSec >> 16;
    m_SectorsPerBlock = SecPerBlockAndBytesPerSec & 0xffff;

    if ( m_SectorsPerBlock != NAND_BLOCK_SIZE || m_BytesPerSector != NAND_SECTOR_SIZE ) goto DoneFlashRestore;

    // Create the backing store for the flash
    m_Flash = new unsigned __int8[m_NumberBlocks*m_SectorsPerBlock*m_BytesPerSector];

    if (m_Flash == NULL || !filer.LZRead(m_Flash, m_NumberBlocks*m_SectorsPerBlock*m_BytesPerSector, hFile)) 
        goto DoneFlashRestore;

    status = true;

DoneFlashRestore:
    if ( hFile!=INVALID_HANDLE_VALUE )
        CloseHandle(hFile);

    if ( m_Flash && !status )
    {
        delete [] m_Flash;
        m_Flash = NULL;
    }

    return status;
}

LPVOID getBinaryResourceFromExe(unsigned int resourceID, unsigned int & size )
{
    HRSRC   hRes = NULL;
    HGLOBAL hgRes = NULL;
    LPVOID  pszRes = NULL;
    HMODULE hExe = GetModuleHandle(NULL);
    // Initialize the area to have size 0
    size = 0;

    hRes = FindResource(hExe, MAKEINTRESOURCE(resourceID),MAKEINTRESOURCE(FLASH_TYPE));
    if ( hRes == NULL )
        goto DoneLoading;

    hgRes = LoadResource(hExe, hRes);
    if ( hgRes == NULL )
        goto DoneLoading;

    pszRes = LockResource(hgRes);
    if ( pszRes == NULL )
        goto DoneLoading;

    size = SizeofResource(hExe, hRes);

DoneLoading:
    ASSERT(pszRes != NULL || (false && HRESULT_FROM_WIN32(GetLastError())));
    return pszRes;
}

// This function loads a compressed image of formated and mounted
// NAND flash contents from the EXE resource. The image is checked in
// at boards\smdk2410\resources\formatted_flash.bin. To generate a fresh
// image:
//  1) Short circuit this function to return false
//  2) Change the compression level in state.cpp LZWrite to 9 instead of 1
//  3) Boot a WinCE image with /flash parameter pointing to a non-existant file
//  4) Format and mount flash using the StorageManager
//  5) Save flash contents to disk using Flash\Save menu option

bool __fastcall IONANDFlashController::InitializeFlashFromResource()
{
    bool status = false;
    unsigned resource_size = 0;
    unsigned __int8 * input = NULL;
    // Create the filer so we can uncompress the flash
    StateFiler filer;
    filer.setStatus(true);

    // Get a pointer to the compressed resource
    input = (unsigned __int8 *)getBinaryResourceFromExe(IDR_FORMATEDFLASH, resource_size);

    // If failed fall back to creating an unformatted array
    if ( input == NULL || resource_size == 0 ) goto DoneFlashInitialize;

    // Flush array should be NULL at this point
    ASSERT(m_Flash == NULL);

    // Read the header information and confirm it is valid
    unsigned __int32 versionAndFlashType = *(unsigned __int32 *)input;
    input += sizeof(unsigned __int32);

    // Version = 0, FlashType = 0
    if ( versionAndFlashType != 0 ) goto DoneFlashInitialize;

    m_NumberBlocks = *(unsigned __int32 *)input;
    input += sizeof(unsigned __int32);

    if ( m_NumberBlocks != NAND_BLOCK_COUNT ) goto DoneFlashInitialize;

    unsigned __int32 BytesPerBlock = *(unsigned __int32*)input;
    input += sizeof(unsigned __int32);

    unsigned __int32 SecPerBlockAndBytesPerSec = *(unsigned __int32*)input;
    input += sizeof(unsigned __int32);
    m_BytesPerSector  = SecPerBlockAndBytesPerSec >> 16;
    m_SectorsPerBlock = SecPerBlockAndBytesPerSec & 0xffff;

    if ( m_SectorsPerBlock != NAND_BLOCK_SIZE || m_BytesPerSector != NAND_SECTOR_SIZE ) goto DoneFlashInitialize;
    if ( m_BytesPerSector*m_SectorsPerBlock != BytesPerBlock ) goto DoneFlashInitialize;

    // Create the backing store for the flash
    m_Flash = new unsigned __int8[m_NumberBlocks*m_SectorsPerBlock*m_BytesPerSector];

    if (m_Flash == NULL || !filer.LZRead(m_Flash, m_NumberBlocks*m_SectorsPerBlock*m_BytesPerSector, input)) 
        goto DoneFlashInitialize;

    status = true;

DoneFlashInitialize:
    if ( m_Flash && !status )
    {
        delete [] m_Flash;
        m_Flash = NULL;
    }

    return status;
}

void __fastcall IONANDFlashController::SaveState(StateFiler& filer) const
{
    filer.Write('FLSH');
    filer.Write(NFCONF);
    filer.Write(NFSTAT);
    filer.Write(NFCMD);
    filer.Write(NFADDR);
    filer.Write(BytesRead);
    unsigned __int32 IsFlashDataPresent = (m_Flash != NULL);
    filer.Write(IsFlashDataPresent);
    if (IsFlashDataPresent)
    {
        filer.Write(m_NumberBlocks);
        filer.Write(m_SectorsPerBlock);
        filer.Write(m_BytesPerSector);
        // Store flash data
        filer.LZWrite(m_Flash,m_NumberBlocks*m_SectorsPerBlock*m_BytesPerSector);
    }
}

void __fastcall IONANDFlashController::RestoreState(StateFiler& filer)
{
    filer.Verify('FLSH');
    filer.Read(NFCONF);
    filer.Read(NFSTAT);
    filer.Read(NFCMD);
    filer.Read(NFADDR);
    filer.Read(BytesRead);
    unsigned __int32 IsFlashDataPresent;
    filer.Read(IsFlashDataPresent);
    if (IsFlashDataPresent)
    {
        filer.Read(m_NumberBlocks);
        filer.Read(m_SectorsPerBlock);
        filer.Read(m_BytesPerSector);

        if ( m_NumberBlocks != NAND_BLOCK_COUNT || m_SectorsPerBlock != NAND_BLOCK_SIZE || m_BytesPerSector != NAND_SECTOR_SIZE ) {
            filer.setStatus(false);
            return;
        }

        // Create the backing store for the flash
        m_Flash = new unsigned __int8[m_NumberBlocks*m_SectorsPerBlock*m_BytesPerSector];
        if (m_Flash) {
            // Read the flash data 
            filer.LZRead(m_Flash,m_NumberBlocks*m_SectorsPerBlock*m_BytesPerSector);
        } else {
            filer.setStatus(false);
        }
    }
}

void __fastcall IONANDFlashController::WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value)
{
    switch (IOAddress) {
    case 0:    // NFCONF
        NFCONF = Value;
        break;

    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        break;
    }
}

void __fastcall IONANDFlashController::WriteByte(unsigned __int32 IOAddress, unsigned __int8 Value)
{
    unsigned int currentCmd   = NFCMD;
    switch (IOAddress) {
    case 0: // NFCONF
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        break;

    case 4: // NFCMD
        // Value is one of the CMD_* constants from smdk2410\inc\nand.h.
        NFCMD = Value;
        BytesRead=0; // reset the bytes-read count after a change of command
        switch (Value) {
        case 0xff:    // CMD_RESET
            NFADDR = 0;
            break;

        case 0x90:  // CMD_READID
            break;
        case 0x50:    // CMD_READ2
            BytesRead = NAND_CONFIG_ADDR; // point at the sector info
            break;
        case 0x00: // CMD_READ
            NFADDR = 0;
            break;

        case 0x60: // CMD_ERASE Erase phase 1
            NFADDR = 0;
            break;
        case 0xd0: // CMD_ERASE2 Erase phase 2
            if ( m_Flash != NULL )
            {
            // Erase all sectors in the NADDRR block
            unsigned int address = NFADDR >> 8;
            ASSERT ( address < m_NumberBlocks*m_SectorsPerBlock);
            if ( address < m_NumberBlocks*m_SectorsPerBlock )
                memset( &m_Flash[address* m_BytesPerSector], 0xFF, m_BytesPerSector );
            }
            break;

        case 0x80: // CMD_WRITE Write phase 1
            if (currentCmd == 0x50 )
                BytesRead = NAND_CONFIG_ADDR;
            NFADDR = 0;
            break;
        case 0x10: // CMD_WRITE2 Write phase 2
            break;

        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            break;
        }
        NFSTAT |= 1; // report that the command has completed
        break;

    case 8: // NFADDR
        NFADDR = (NFADDR >> 8) + (((int)Value) << 24);
        break; // ignore the NFADDR

    case 0xc: // NFDATA
        if ( NFCMD == 0x80 && m_Flash != NULL )
        {
            // Write one byte to the NADDRR page
            ASSERT ( (NFADDR >> 8) < m_NumberBlocks*m_SectorsPerBlock && BytesRead < m_BytesPerSector);
            if ((NFADDR >> 8) < m_NumberBlocks*m_SectorsPerBlock && BytesRead < m_BytesPerSector)    
                m_Flash[ ((NFADDR & 0xff)+(NFADDR >> 8))* m_BytesPerSector + BytesRead ] = Value;
            BytesRead++;
        }
        else
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE); // Writing to NFDATA with command != WRITE
        break;

    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE); // Writing to NFSTAT
        break;
    }
}

unsigned __int32 __fastcall IONANDFlashController::ReadWord(unsigned __int32 IOAddress)
{
    switch (IOAddress) {
    case 0: // NFCONF
        return NFCONF;

    case 0x10: // NFSTAT
        return NFSTAT;

    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        return 0;
    }
}

unsigned __int8 __fastcall IONANDFlashController::ReadByte(unsigned __int32 IOAddress)
{
    switch (IOAddress) {
    case 0xc: // NFDATA
        switch (NFCMD) {
        case 0x90:    // CMD_READID
            if (BytesRead == 0) {
                BytesRead++;
                return 0xec; // Manufacturer
            } else {
                return 0x76; // Device ID
            }

        case 0x50: // CMD_READ2
            if ( m_Flash != NULL && (NFADDR >> 8) < (m_NumberBlocks*m_SectorsPerBlock) )
            {
                ASSERT((BytesRead - NAND_CONFIG_ADDR) < 10 && (NFADDR & 0xff) == 0);

                // Provide confirmation
                if (BytesRead >= m_BytesPerSector ) return 1;

                unsigned __int8 val = m_Flash[(NFADDR >> 8)*m_BytesPerSector+BytesRead];
                BytesRead++;
                return val;
            }
            else
                return 0x0; // report the block is bad.  A result of 0xff indicates the block is good.

        case 0x0: // CMD_READ
            if ( m_Flash != NULL )
            {
                ASSERT(BytesRead < m_BytesPerSector);
                unsigned __int8 val = 1;
                if ((NFADDR >> 8) < m_NumberBlocks*m_SectorsPerBlock && BytesRead < m_BytesPerSector)
                    val = m_Flash[ (((NFADDR & 0xff)+(NFADDR >> 8))*m_BytesPerSector)+ BytesRead];
                BytesRead++;
                return val;
            }

        case 0x10: // CMD_WRITE2 Write phase 2
        case 0xD0: // CMD_ERASE2 Erase phase 2
            return 0x0; // Confirm that the operation completed succefully

        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            break;
        }
        return 0;

    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        return 0;
    }
}

void IONANDFlashController::setSavedFileName(__in_z const wchar_t * filename)
{
    wchar_t *temp = filename != NULL ? _wcsdup(filename) : NULL;

    if (temp == NULL && filename != NULL) {
        TerminateWithMessage(ID_MESSAGE_RESOURCE_EXHAUSTED);
    }

    wchar_t *old_name = m_SavedFileName;
    m_SavedFileName = temp;
    free(old_name);
}

IOAM29LV800BB::IOAM29LV800BB()
{
    CMD = 0xF0F0;
}
void IOAM29LV800BB::InitializeEbootConfig(unsigned __int16* MacAddress)
{
    if (FlashBank0[0x3c000] != 0x12345678) {
        // Invalid EBOOT config signature.
        //
        // This sets up a reasonable-looking EBOOT config structure in flash memory.  
        // It has default settings, along with the specified MAC address.  The
        // EBOOT config structure is located at flash address 0xf0000, however
        // FlashBank0 is an array of __int32, so addresses are divided by 4.
        FlashBank0[0x3c000]=0x12345678;
        FlashBank0[0x3c001]=0x00040001;
        FlashBank0[0x3c002]=0x00000002;
        FlashBank0[0x3c005]=0x00000005;
    }
    // Copy the MAC address into the EBOOT config structure.
    memcpy((void*)(((size_t)FlashBank0)+0xf0016), MacAddress, 6);
}


void __fastcall IOAM29LV800BB::SaveState(StateFiler& filer) const
{
    filer.Write('AM29');
    filer.Write(CMD);
}

void __fastcall IOAM29LV800BB::RestoreState(StateFiler& filer)
{
    filer.Verify('AM29');
    filer.Read(CMD);
}

// This routine is the inverse of GetSectorSize() from am29vl800.c
size_t __fastcall IOAM29LV800BB::SectorSizeFromAddress(unsigned __int32 IOAddress)
{
    if (IOAddress < 16*1024) {
        return 16*1024;
    } else if (IOAddress < 24*1024) {
        return 8*1024;
    } else if (IOAddress < 32*1024) {
        return 8*1024;
    } else if (IOAddress < 64*1024) {
        return 32*1024;
    } else {
        return 64*1024;
    }
}

void __fastcall IOAM29LV800BB::WriteHalf(unsigned __int32 IOAddress, unsigned __int16 Value)
{
    // Clear this value here so we don't have to do it in the hot code path
    BoardIOAddressAdj = 0;
    switch (IOAddress) {
    case 0x5555<<1: // AMD_CMD_ADDR and AMD_UNLOCK_ADDR1
        // Check if we are currently in a middle of a write
        if ( CMD != 0xA0A0 ) // !AMD_CMD_PROGRAM (in theory we should also say 
                             // !AMD_CMD_SECTERASE but 0x5555<<1 is not a sector address)
        {
            // Transitioning from AutoSelect state into Read/Idle state
            if (Value == 0xF0F0 && CMD == 0x9090) {
                // Restore the actual value of address which we initialized with Dev ID and Man. ID
                FlashBank0[0]= CachedDataValue;
            }
            // Transitioning from AMD_UNLOCK_ADDR2 into AutoSelect state
            if (Value == 0x9090 && CMD == 0xAAAA) {
                // Bytes 0/1: Manufacturer code 0x01
                // Bytes 2/3: Device code 0x225b (AMD29LV800BB)
                // These bytes will be read by subsequent ARM instructions.
                CachedDataValue = FlashBank0[0];
                FlashBank0[0]= 0x225b0001;
            }
            // Transitioning from AMD_CMD_SECTERASE into AMD_UNLOCK_ADDR1_ERASE
            if (Value == 0xAAAA && CMD == 0x8080) {
                CMD = 0xAAAB;
            }
            else
                CMD = Value;
            break;
        }

    default:
        // The behavior of the write depends on the currently-selected command
        switch (CMD) {
            case 0xf0f0:    // AMD_CMD_RESET
            // writes after a reset are ignored
            break;            

        case 0x9090:        // AMD_CMD_AUTOSEL
            // writes after an autosel are ignored
            break;

        case 0xa0a0:        // AMD_CMD_PROGRAM
            // Write after AMD_CMD_PROGRAM stores the value into flash memory
            *(unsigned __int16*)((size_t)FlashBank0+IOAddress) = Value;
            CMD = 0xF0F0;
            break;

        case 0xaaaa:        // AMD_CMD_UNLOCK1 to AMD_UNLOCK_UNLOCK2 transition
            if ( Value != 0x5555 || IOAddress != (0x2aaa<<1) )
                TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            break;

        case 0xaaab:        // AMD_CMD_UNLOCK1_ERASE to AMD_UNLOCK_UNLOCK2_ERASE transition
            if ( Value != 0x5555 || IOAddress != (0x2aaa<<1) )
                TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            CMD = 0x8080;
            break;

        case 0x8080:        // AMD_CMD_SECTERASE
            // Write after SECTERASE contains the value AMD_CMD_SECTERASE_CONFIRM, and
            // the address is the address of the sector to erase.
            if (Value == 0x3030) { // AMD_CMD_SECTERASE_CONFIRM
                memset((void*)((size_t)FlashBank0+IOAddress), 0xff, SectorSizeFromAddress(IOAddress));
            }
            CMD = 0xF0F0;
            break;

        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        }
        break;
    }
}

IOSPI::IOSPI()
{
    SPSTA.Word=1;
    SPPIN.Word=2;
}

void __fastcall IOSPI::SaveState(StateFiler& filer) const
{
    filer.Write('ISPI');
    filer.Write(SPCON.Word);
    filer.Write(SPSTA.Word);
    filer.Write(SPPIN.Word);
    filer.Write(SPPRE);
    filer.Write(SPTDAT);
    filer.Write(SPRDAT);
}

void __fastcall IOSPI::RestoreState(StateFiler& filer)
{
    filer.Verify('ISPI');
    filer.Read(SPCON.Word);
    filer.Read(SPSTA.Word);
    filer.Read(SPPIN.Word);
    filer.Read(SPPRE);
    filer.Read(SPTDAT);
    filer.Read(SPRDAT);
}

void __fastcall IOSPI1::SaveState(StateFiler& filer) const
{
    filer.Write('SPI1');
    IOSPI::SaveState(filer);
    filer.Write(KeyboardQueue);
    filer.Write(QueueHead);
    filer.Write(QueueTail);
}

void __fastcall IOSPI1::RestoreState(StateFiler& filer)
{
    filer.Verify('SPI1');
    IOSPI::RestoreState(filer);
    filer.Read(KeyboardQueue);
    filer.Read(QueueHead);
    filer.Read(QueueTail);
    if (QueueHead >= ARRAY_SIZE(KeyboardQueue) || QueueTail >= ARRAY_SIZE(KeyboardQueue)) {
        // Security: don't allow the queue head/tail to point outside of the queue array itself
        QueueHead = QueueTail = 0;
    }
}

// SPI1 is the PS/2 keyboard
void __fastcall IOSPI1::WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value)
{
    switch (IOAddress) {
    case 0:
        SPCON.Word = Value;
        ASSERT(SPCON.Bits.Reserved1==0);
        break;

    case 4: 
        break;    // SPSTA is readonly

    case 8:
        SPPIN.Word = Value;
        ASSERT(SPPIN.Bits.Reserved1==1);
        ASSERT(SPPIN.Bits.Reserved2==0);
        break;

    case 0xc:
        SPPRE = Value;
        break;

    case 0x10:
        SPTDAT = (unsigned __int8)Value;
        SPSTA.Bits.REDY=1;    // report that we're ready to transmit/receive more data
        break;
    
    case 0x14:
        SPRDAT = (unsigned __int8)Value;
        break;

    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
    }
}

unsigned __int32 __fastcall IOSPI1::ReadWord(unsigned __int32 IOAddress)
{
    switch (IOAddress) {
    case 0:
        return SPCON.Word;

    case 4:
        return SPSTA.Word;

    case 8:
        return SPPIN.Word;

    case 0xc:
        return SPPRE;

    case 0x10:
        return SPTDAT;

    case 0x14:
        // getsFromKBCTL writes 0xff to SPTDAT then reads from SPRDAT to retrieve
        // keys from the keyboard queue.
        if (SPTDAT == 0xff && QueueHead != QueueTail) {
            SPRDAT = KeyboardQueue[QueueTail];
            QueueTail = (QueueTail+1) % KEYBOARD_QUEUE_LENGTH;
            if (QueueHead != QueueTail) {
                // The queue isn't empty yet.  Signal another keyboard interrupt.
                InterruptController.RaiseInterrupt(InterruptController.SourceEINT1);
            }
        } else {
            SPRDAT = 0;
        }
        return SPRDAT;

    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        return 0;
    }
}

void __fastcall IOSPI1::EnqueueKey(unsigned __int8 Key)
{
    EnterCriticalSection(&IOLock);
    if (SPCON.Bits.SMOD != 1) {
        // keyboard is disabled
        LeaveCriticalSection(&IOLock);
        return;
    }
    KeyboardQueue[QueueHead]=Key;
    QueueHead = (QueueHead+1) % KEYBOARD_QUEUE_LENGTH;
    // Signal the CPU that a keyboard interrupt is pending
    InterruptController.RaiseInterrupt(InterruptController.SourceEINT1);
    LeaveCriticalSection(&IOLock);
}

IOLCDController::IOLCDController()
{
    m_NumLockState = true;
}

bool __fastcall IOLCDController::Reset()
{
    LCDINTMSK.Word=3;
    LPCSEL.Word=4;
    m_NumLockState = true;
    return true;
}

bool __fastcall IOLCDController::PowerOn()
{
    LCDINTMSK.Word=3;
    LPCSEL.Word=4;

    if (!__super::PowerOn()) {
        return false;
    }

    // Register custom menu callbacks
    RegisterMenuCallback(ID_FLASH_SAVE, IOLCDController::onID_FLASH_SAVE);

    if (!CreateLCDThread()) {
        return false;
    }
    if (!CreateLCDDMAThread()) {
        return false;
    }

    return true;
}

void __fastcall IOLCDController::SaveState(StateFiler& filer) const
{
    filer.Write('LCDC');
    filer.Write(LCDCON1.Word);
    filer.Write(LCDCON2.Word);
    filer.Write(LCDCON3.Word);
    filer.Write(LCDCON4.Word);
    filer.Write(LCDCON5.Word);
    filer.Write(LCDSADDR1.Word);
    filer.Write(LCDSADDR2.Word);
    filer.Write(LCDSADDR3.Word);
    filer.Write(REDVAL);
    filer.Write(GREENVAL);
    filer.Write(BLUEVAL);
    filer.Write(DITHMODE);
    filer.Write(TPAL.Word);
    filer.Write(LCDINTPND.Word);
    filer.Write(LCDSRCPND.Word);
    filer.Write(LCDINTMSK.Word);
    filer.Write(LPCSEL.Word);
    filer.Write(m_NumLockState);
    IOWinController::SaveState( filer );
}

void __fastcall IOLCDController::RestoreState(StateFiler& filer)
{
    filer.Verify('LCDC');
    filer.Read(LCDCON1.Word);
    filer.Read(LCDCON2.Word);
    filer.Read(LCDCON3.Word);
    filer.Read(LCDCON4.Word);
    filer.Read(LCDCON5.Word);
    filer.Read(LCDSADDR1.Word);
    filer.Read(LCDSADDR2.Word);
    filer.Read(LCDSADDR3.Word);
    filer.Read(REDVAL);
    filer.Read(GREENVAL);
    filer.Read(BLUEVAL);
    filer.Read(DITHMODE);
    filer.Read(TPAL.Word);
    filer.Read(LCDINTPND.Word);
    filer.Read(LCDSRCPND.Word);
    filer.Read(LCDINTMSK.Word);
    filer.Read(LPCSEL.Word);    
    filer.Read(m_NumLockState);
    IOWinController::RestoreState( filer );
}

void __fastcall IOLCDController::WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value)
{
    switch (IOAddress) {
    case 0:
        // Expect TFT LCD Panel, 16bpp
        LCDCON1.Word = Value;
        // when powering down, these ASSERTs fail.
        //ASSERT(LCDCON1.Bits.PNRMODE==3);   // TFT
        //ASSERT(LCDCON1.Bits.MMODE==0);
        //ASSERT(LCDCON1.Bits.Reserved1==0);
        // All settings are in place - enable or disable the LCD controller
        if (LCDCON1.Bits.ENVID) {
            ShowWindow();
        } else {
            HideWindow();
        }
        break;

    case 4:
        LCDCON2.Word = Value;
        break;

    case 8:
        LCDCON3.Word = Value;
        ASSERT(LCDCON3.Bits.Reserved1==0);
        break;

    case 0xc:
        LCDCON4.Word = Value;
        ASSERT(LCDCON4.Bits.Reserved1==0);
        break;

    case 0x10:
        LCDCON5.Word = Value;
        ASSERT(LCDCON5.Bits.Reserved1==0);
        ASSERT(LCDCON5.Bits.Reserved2==0);
        break;

    case 0x14:
        LCDSADDR1.Word = Value;
        ASSERT(LCDSADDR1.Bits.Reserved1==0);
        break;

    case 0x18:
        LCDSADDR2.Word = Value;
        ASSERT(LCDSADDR2.Bits.Reserved1==0);
        break;

    case 0x1c:
        LCDSADDR3.Word = Value;
        ASSERT(LCDSADDR3.Bits.OFFSIZE==0);
        ASSERT(LCDSADDR3.Bits.Reserved1==0);
        break;

    case 0x20:
        REDVAL = Value;
        break;

    case 0x24:
        GREENVAL = Value;
        break;

    case 0x28:
        BLUEVAL = Value;
        break;

    case 0x2c:
        if ( Value >= 0 && Value < 4)
        {
            PostMessage(hWnd, WM_COMMAND, (WPARAM)ID_COMMAND_ROTATE, (LPARAM)Value);
            break;
        }
    case 0x30:
    case 0x34:
    case 0x38:
    case 0x3c:
    case 0x40:
    case 0x44:
    case 0x48:
        // These are reserved for "Test mode" according to the docs
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        break;

    case 0x4c:
        DITHMODE = Value;
        break;

    case 0x50:
        TPAL.Word = Value;
        ASSERT(TPAL.Bits.Reserved1==0);
        break;

    case 0x54:
        LCDINTPND.Word = Value;
        ASSERT(LCDINTPND.Bits.Reserved1==0);
        break;

    case 0x58:
        LCDSRCPND.Word = Value;
        ASSERT(LCDSRCPND.Bits.Reserved1==0);
        break;

    case 0x5c:
        LCDINTMSK.Word = Value;
        ASSERT(LCDINTMSK.Bits.Reserved1==0);
        break;

    case 0x60:
        LPCSEL.Word = Value;
        ASSERT(LPCSEL.Bits.Reserved1==0);
        break;

    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
    }
}

unsigned __int32 __fastcall IOLCDController::ReadWord(unsigned __int32 IOAddress)
{
    switch (IOAddress) {
    case 0:
        return LCDCON1.Word;

    case 4:
        return LCDCON2.Word;

    case 8:
        return LCDCON3.Word;

    case 0xc:
        return LCDCON4.Word;

    case 0x10:
        return LCDCON5.Word;

    case 0x14:
        return LCDSADDR1.Word;

    case 0x18:
        return LCDSADDR2.Word;

    case 0x1c:
        return LCDSADDR3.Word;

    case 0x20:
        return REDVAL;

    case 0x24:
        return GREENVAL;

    case 0x28:
        return BLUEVAL;

    case 0x2c:
    case 0x30:
    case 0x34:
    case 0x38:
    case 0x3c:
    case 0x40:
    case 0x44:
    case 0x48:
        // These are reserved for "Test mode" according to the docs
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        return 0;

    case 0x4c:
        return DITHMODE;

    case 0x50:
        return TPAL.Word;

    case 0x54:
        return LCDINTPND.Word;

    case 0x58:
        return LCDSRCPND.Word;

    case 0x5c:
        return LCDINTMSK.Word;

    case 0x60:
        return LPCSEL.Word;

    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        return 0;
    }
}

bool IOLCDController::LCDEnabled()
{
    return LCDCON1.Bits.ENVID;
}

size_t IOLCDController::getFrameBuffer()
{
    return (LCDSADDR1.Bits.LCDBASEU << 1) | (LCDSADDR1.Bits.LCDBANK << 22);
}

void IOLCDController::onWM_LBUTTONDOWN( LPARAM lParam )
{
    EnterCriticalSection(&IOLock);
    ADConverter.SetPenState(IOADConverter::PenStateDown);
    ADConverter.SetPenSample(LOWORD(lParam), HIWORD(lParam));
    ADConverter.RaiseTCInterrupt();
    LeaveCriticalSection(&IOLock);
}
void IOLCDController::onWM_MOUSEMOVE( LPARAM lParam )
{
    EnterCriticalSection(&IOLock);
    ADConverter.SetPenSample(LOWORD(lParam), HIWORD(lParam));
    LeaveCriticalSection(&IOLock);
}
void IOLCDController::onWM_LBUTTONUP( LPARAM lParam )
{
    EnterCriticalSection(&IOLock);
    ADConverter.SetPenSample(LOWORD(lParam), HIWORD(lParam));
    ADConverter.SetPenState(IOADConverter::PenStateUp);
    ADConverter.RaiseTCInterrupt();
    LeaveCriticalSection(&IOLock);
}
void IOLCDController::onWM_CAPTURECHANGED()
{
    EnterCriticalSection(&IOLock);
    // Change the state of pen if the mouse up event was not delivered to the emulator
    if ( ADConverter.GetPenState() == IOADConverter::PenStateUp )
    {
        ADConverter.SetPenState(IOADConverter::PenStateUp);
        ADConverter.RaiseTCInterrupt();
    }
    LeaveCriticalSection(&IOLock);
}

void IOLCDController::onWMKEY( unsigned __int8 vKey, unsigned __int8 & KeyUpDown )
{
    if (vKey == VK_SHIFT) {
        vKey = VK_LSHIFT;
    }

    unsigned __int32 FuncKeyCode = Configuration.FuncKeyCode != 0 &&
                                   Configuration.FuncKeyCode < ARRAY_SIZE(VKtoSCTable) ?
                                   Configuration.FuncKeyCode : VK_MATRIX_FN;
    if (vKey) {
        if ( VKtoSCTable[vKey] )
        {
            SPI1.EnqueueKey(VKtoSCTable[vKey] | KeyUpDown);
        }
        else
        {
            // Search through the function key table for a match
            unsigned __int8 pvkMapMatch = 0;
            for (int ui = 0; ui < sizeof(g_rgvkMapFn)/sizeof(VirtualKeyMapping); ++ui)
                if (g_rgvkMapFn[ui].uiVk == vKey) 
                {
                    pvkMapMatch = (unsigned __int8)g_rgvkMapFn[ui].uiVkGenerated;
                    break;
                }
            // If the key is not found search through the numlock key table
            if (pvkMapMatch == 0)
            {
                for (int ui = 0; ui < sizeof(g_rgvkMapNumLock)/sizeof(VirtualKeyMapping); ++ui)
                    if (g_rgvkMapNumLock[ui].uiVk == vKey) 
                    {
                        pvkMapMatch = (unsigned __int8)g_rgvkMapNumLock[ui].uiVkGenerated;
                        break;
                    }
                // Check if we need to update the emulator state to match the desktop
                // numlock setting, which currently has numlock off. If the guest currently
                // has the numlock on - simulate a numlock key press to change the setting
                if ( pvkMapMatch && m_NumLockState)
                {
                    SPI1.EnqueueKey(VKtoSCTable[FuncKeyCode] | 0);
                    SPI1.EnqueueKey(VKtoSCTable[VK_HYPHEN] | 0);
                    SPI1.EnqueueKey(VKtoSCTable[VK_HYPHEN] | 0x80);
                    SPI1.EnqueueKey(VKtoSCTable[FuncKeyCode] | 0x80);
                    m_NumLockState = false;
                }
            }

            if (pvkMapMatch)
            {
                // Update guest OS NumLock state
                if ( pvkMapMatch == VK_HYPHEN && KeyUpDown == 0)
                    m_NumLockState = !m_NumLockState;
                SPI1.EnqueueKey(VKtoSCTable[FuncKeyCode] | 0);
                SPI1.EnqueueKey(VKtoSCTable[pvkMapMatch] | KeyUpDown);
                SPI1.EnqueueKey(VKtoSCTable[FuncKeyCode] | 0x80);
            }
            else
            {
                // At this point we have a key code which can't be translated by the
                // Samsung keyboard driver, so we either have to act upon it in the 
                // emulator or ignore it
                if ( vKey == EMUL_RESET )
                {
                    if ( !KeyUpDown )
                        PostMessage(hWnd, WM_COMMAND, (WPARAM)ID_FILE_RESET_SOFT, (LPARAM)0);
                }
                // else
                // Ignore the key since there is a set of valid VK_* found on the keyboard 
                // that can't be translated 
                // ASSERT(false && "Didn't map VKey to samsung scan code");
            }
        }
    } // else the Win32 VKey doesn't correspond to any key on the Samsung keyboard.  Ignore it.
}

void BoardReset(bool hardReset)
{
    EnterCriticalSection(&IOLock);
    if (hardReset) {
        CpuSetResetPending();
        LCDController.resetToDefaultOrientation();
    } else {
        InterruptController.RaiseInterrupt(IOInterruptController::SourceEINT2);  // EINT2 is the reset button
    }
    // Reset the peripheral devices
    if (!ResetDevices())
        TerminateWithMessage(ID_MESSAGE_INTERNAL_ERROR);
    // Write out board configuration, notifying the BSP of the kind of reset
    BoardWriteGuestArguments((WORD)Configuration.ScreenWidth, (WORD)Configuration.ScreenHeight,
                             (WORD)Configuration.ScreenBitsPerPixel, !hardReset);
    LeaveCriticalSection(&IOLock);
}

void BoardSuspend(void)
{
    GPIO.PowerButtonPressed();
}


DWORD WINAPI IOLCDController::LCDDMAThreadProc(void)
{
    unsigned __int32 *HostFrameBuffer;
    HDC hDC;
    char BMIData[sizeof(BITMAPINFO)+3*sizeof(DWORD)];
    BITMAPINFO *bmi;
    bool fHostWindowNeedsUpdate;
    unsigned __int32 *pScreen=NULL;

    int ScreenXSize = 0;
    int ScreenYSize = 0;
    int BytesPerPixel = 0;

    ASSERT(hBitMap == 0);

    for (;;) {
LoopStart:
        WaitForSingleObject(LCDEnabledEvent, INFINITE);
        
        HostFrameBuffer = NULL;

        hDC = GetDC(hWnd);
        if (!hDC) {
            ShowDialog(ID_MESSAGE_RESOURCE_EXHAUSTED);
            exit(1);
        }

        if (hBitMap == 0) {
            // Read in the information about the screen
            ScreenXSize = (int)ScreenX();
            ScreenYSize = (int)ScreenY();
            BytesPerPixel = (int)BitsPerPixel() >> 3;
            // Create the windows BMP
            bmi = (BITMAPINFO*)BMIData;
            bmi->bmiHeader.biWidth=ScreenXSize;
            bmi->bmiHeader.biHeight=-ScreenYSize; // create a top-down bitmap
            bmi->bmiHeader.biPlanes=1;
            bmi->bmiHeader.biSizeImage=0;
            bmi->bmiHeader.biXPelsPerMeter=0;
            bmi->bmiHeader.biYPelsPerMeter=0;
            bmi->bmiHeader.biClrUsed=0;
            bmi->bmiHeader.biClrImportant=0;
            bmi->bmiHeader.biBitCount=(WORD)BytesPerPixel << 3;
            if (BytesPerPixel == 2) {
                bmi->bmiHeader.biSize=sizeof(BITMAPINFO) +3*sizeof(DWORD);
                bmi->bmiHeader.biCompression=BI_BITFIELDS;
                *(DWORD*)&bmi->bmiColors[0] = 0xf800; // red mask for 565 encoded pixels
                *(DWORD*)&bmi->bmiColors[1] = 0x07e0; // green mask
                *(DWORD*)&bmi->bmiColors[2] = 0x001f; // blue mask
            } else {
                bmi->bmiHeader.biSize=sizeof(BITMAPINFO);
                bmi->bmiHeader.biCompression=BI_RGB;
            }

            hBitMap = CreateDIBSection(hDC, bmi, DIB_RGB_COLORS, (LPVOID*)&pScreen, NULL, 0);
            if (!hBitMap) {
                ShowDialog(ID_MESSAGE_RESOURCE_EXHAUSTED);
                exit(1);
            }
        }
        else if ( ScreenXSize != (int)ScreenX() || ScreenYSize != (int)ScreenY() ||
                  BytesPerPixel != (int)BitsPerPixel() >> 3 )
        {
            if ( LCDEnabled() ) 
            {
                // the LCD size has changed under us - exit gracefully
                TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            }
            else
                goto LoopStart;
        }

        fHostWindowNeedsUpdate=true;

        while (LCDEnabled()) {

            if (!HostFrameBuffer) {
                // The frame buffer hasn't been mapped in by software already.  Try now.
                unsigned __int32 FrameBuffer;

                FrameBuffer = (unsigned __int32)getFrameBuffer();
                HostFrameBuffer = (unsigned __int32 *)BoardMapGuestPhysicalToHostRAM(FrameBuffer);
                if (HostFrameBuffer == NULL) {
                    // draw a flickering screen so we know that the LCD is active but without
                    // access to a frame buffer.
                    static unsigned __int8 c;
                    memset(pScreen, c, ScreenXSize*ScreenYSize*BytesPerPixel);
                    c++;
                    fHostWindowNeedsUpdate=true;
                }
            }

            if (HostFrameBuffer) {
                // Copy pixels from the WinCE frame buffer to the Windows DIB
                for (int i=0; i<ScreenXSize*ScreenYSize*BytesPerPixel/(int)sizeof(unsigned __int32); ++i) {
                    unsigned __int32 HostBytes;

                    HostBytes = HostFrameBuffer[i];
                    if (pScreen[i] != HostBytes) {
                        fHostWindowNeedsUpdate=true;
                    }
                    pScreen[i] = HostBytes;
                }
            }

            // Render the DIB to the screen
            if (fHostWindowNeedsUpdate) {
                RECT DeviceView; getDeviceView( DeviceView );
                InvalidateRect(hWnd, &DeviceView, FALSE);
                fHostWindowNeedsUpdate=false;
            }

            Sleep(82); // 41ms sleep gives approx 24 frames per second
        }
        //Only fill the LCD with blackness if we are not saving state.
        if (!bSaveState)
        {

            // The LCD has been disabled.  Simulate that look&feel by filling the
            // window rectangle with blackness.
            memset(pScreen, 0, ScreenXSize*ScreenYSize*BytesPerPixel);
        }    
        RECT DeviceView; 
        getDeviceView( DeviceView );
        InvalidateRect(hWnd, &DeviceView, FALSE);

    }
}


bool IOLCDController::onID_FLASH_SAVE(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    wchar_t * flashFileName;
    if (Configuration.isFlashStateFileSpecified() )
       flashFileName = Configuration.getFlashStateFile();
    else
       flashFileName = L"temp.bin";

    // Check if there is currently a save in progress
    if ( NANDFlashController.incrementSavedStateLock() > 1 )
        goto ExitDontSave;

    // Set the name on the flash controller
    NANDFlashController.setSavedFileName(flashFileName);

    // Startup a thread to save the flash
    HANDLE hThread = INVALID_HANDLE_VALUE;
    hThread=CreateThread(NULL, 0, IONANDFlashController::NANDFlashSaveStatic, 
                        &NANDFlashController, 0, NULL);
    // If we failed to create the thread save the flash on the current thread
    if (hThread == INVALID_HANDLE_VALUE) {
        IONANDFlashController::NANDFlashSaveStatic(&NANDFlashController);
    }
    else
        CloseHandle(hThread);

    return true;

ExitDontSave:
    NANDFlashController.decrementSavedStateLock();
    return false;
}

void __fastcall IODMAController::SaveState(StateFiler& filer) const
{
    filer.Write('DMAC');
    filer.Write(DISRC.Word);
    filer.Write(DISRCC.Word);
    filer.Write(DIDST.Word);
    filer.Write(DIDSTC.Word);
    filer.Write(DCON.Word);
    filer.Write(DSTAT.Word);
    filer.Write(DCSRC.Word);
    filer.Write(DCDST.Word);
    filer.Write(DMASKTRIG.Word);
}

void __fastcall IODMAController::RestoreState(StateFiler& filer)
{
    filer.Verify('DMAC');
    filer.Read(DISRC.Word);
    filer.Read(DISRCC.Word);
    filer.Read(DIDST.Word);
    filer.Read(DIDSTC.Word);
    filer.Read(DCON.Word);
    filer.Read(DSTAT.Word);
    filer.Read(DCSRC.Word);
    filer.Read(DCDST.Word);
    filer.Read(DMASKTRIG.Word);
}

void __fastcall IODMAController::WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value)
{
      switch (IOAddress) {
      case 0:
              DISRC.Word = Value;
              ASSERT(DISRC.Bits.Reserved1==0);
              break;

      case 4:
              DISRCC.Word = Value;
              ASSERT(DISRCC.Bits.Reserved1==0);
              break;

      case 8:
              DIDST.Word = Value;
              ASSERT(DIDST.Bits.Reserved1==0);
              break;

      case 0xc:
              DIDSTC.Word = Value;
              ASSERT(DIDSTC.Bits.Reserved1==0);
              break;

      case 0x10:
              DCON.Word = Value;
              break;

      case 0x14:
              break; // DSTAT is readonly

      case 0x18:
              break; // DCSRC is readonly

      case 0x1c:
              break; // DCDST is readonly

      case 0x20:
              DMASKTRIG.Word = Value;
              ASSERT(DMASKTRIG.Bits.Reserved1==0);
              break;

      default:
              TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
              break;
      }
}

unsigned __int32 __fastcall IODMAController::ReadWord(unsigned __int32 IOAddress){
      switch (IOAddress) {
      case 0:
              return DISRC.Word;

      case 4:
              return DISRCC.Word;

      case 8:
              return DIDST.Word;

      case 0xc:
              return DIDSTC.Word;

      case 0x10:
              return DCON.Word;

      case 0x14:
              return DSTAT.Word;

      case 0x18:
              return DCSRC.Word;

      case 0x1c:
              return DCDST.Word;

      case 0x20:
              return DMASKTRIG.Word;

      default:
              TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
              return 0;
      }
}

// DMA1 is used for audio input.
void __fastcall IODMAController1::WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value)
{
    //printf("DMA write at %x with %x \n", IOAddress, Value );
    switch (IOAddress) {

        case 8:
            DIDST.Word = Value;
            // If the DMA channel is enabled and the auto reload is on queue the block
            if ( DMASKTRIG.Bits.ON_OFF && DCON.Bits.RELOAD == 0)
            {
                // Queue the buffer for output
                IIS.QueueInput(
                    (void *)(SIZE_T)BoardMapGuestPhysicalToHostRAM((unsigned int)DIDST.Bits.D_ADDR), 
                    DSTAT.Word*(DCON.Bits.DSZ << 1));
            }
            break;

        case 0x20:
            DMASKTRIG.Word = Value;
            if ( DMASKTRIG.Bits.ON_OFF && !DMASKTRIG.Bits.STOP)
            {
                // Verify that the DMA configuration is supported
                bool InputDMA = true;
                size_t Host_D_ADDR = BoardMapGuestPhysicalToHostRAM(DIDST.Bits.D_ADDR);
                // Check that the dest is supported (Buffer from system bus)
                if ( Host_D_ADDR == 0 || DIDSTC.Bits.INC != 0 || DIDSTC.Bits.LOC != 0)
                    InputDMA = false;
                // Check that the source is supported (IIS FIFO)
                if ( DISRC.Bits.S_ADDR != 0x55000010 || DISRCC.Bits.INC != 1 || DISRCC.Bits.LOC != 1)
                    InputDMA = false;
                // Check that the channel configuration is supported
                if ( DCON.Bits.SERVMODE != 0 || DCON.Bits.HWSRCSEL != 2 || DCON.Bits.SWHW_SEL != 1 || 
                     DCON.Bits.RELOAD != 0 || DCON.Bits.DSZ != 1 || DCON.Bits.TC <= 0 )
                    InputDMA = false;

                if ( !InputDMA )
                    TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);

                DSTAT.Word = DCON.Bits.TC;

                // Deal with the sound that may be currently playing
                IIS.SetInputDMA(true);

                // Queue the buffer for output
                IIS.QueueInput(
                    (void *)Host_D_ADDR, 
                    DSTAT.Word*(DCON.Bits.DSZ << 1));

            }
            else
                IIS.SetInputDMA(false);
            break;
        default:
            __super::WriteWord(IOAddress, Value);
    }
}

// DMA2 is used for audio output. It could also be used for input but the driver
// doesn't support that.
void __fastcall IODMAController2::WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value)
{
    //printf("DMA write at %x with %x \n", IOAddress, Value );
    switch (IOAddress) {

        case 0:
            DISRC.Word = Value;
            // If the DMA channel is enabled and the auto reload is on queue the block
            if ( DMASKTRIG.Bits.ON_OFF && DCON.Bits.RELOAD == 0)
            {
                // Queue the buffer for output
                IIS.QueueOutput(
                    (void *)(SIZE_T)BoardMapGuestPhysicalToHostRAM((unsigned int)DISRC.Bits.S_ADDR), 
                    DSTAT.Word*(DCON.Bits.DSZ << 1));
            }
            break;

        case 0x20:
            DMASKTRIG.Word = Value;
            if ( DMASKTRIG.Bits.ON_OFF )
            {
                //printf("Enabled DMA2\n");
                // Verify that the DMA configuration is supported

                bool OutputDMA = true;
                size_t Host_S_ADDR = BoardMapGuestPhysicalToHostRAM(DISRC.Bits.S_ADDR);
                // Check that the source is supported
                if ( Host_S_ADDR == 0 || DISRCC.Bits.INC != 0 || DISRCC.Bits.LOC != 0)
                    OutputDMA = false;
                // Check that the dest is supported
                if ( DIDST.Bits.D_ADDR != 0x55000010 || DIDSTC.Bits.INC != 1 || DIDSTC.Bits.LOC != 1)
                    OutputDMA = false;
                // Check that the channel configuration is supported
                if ( DCON.Bits.SERVMODE != 0 || DCON.Bits.HWSRCSEL != 0 || DCON.Bits.SWHW_SEL != 1 || 
                     DCON.Bits.RELOAD != 0 || DCON.Bits.DSZ != 1 || DCON.Bits.TC <= 0 )
                    OutputDMA = false;

                if ( !OutputDMA )
                    TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);


                DSTAT.Word = DCON.Bits.TC;

                // Enable the generation of interrupts on IIS bus
                IIS.SetOutputDMA(true);

                // Queue the buffer for output
                IIS.QueueOutput(
                    (void *)Host_S_ADDR, 
                    DSTAT.Word*(DCON.Bits.DSZ << 1));

            }
            else
            {
                //printf("Disabled DMA2\n"); 
                // Disable the generation of interrupts on IIS bus
                IIS.SetOutputDMA(false);
                // Do queue flush
                IIS.FlushOutput();
            }
            break;
        default:
            __super::WriteWord(IOAddress, Value);
    }
}

IOADConverter::IOADConverter()
{
    Reset();
}

bool __fastcall IOADConverter::Reset(void)
{
    ADCCON.Word=0x3fc4;
    ADCTSC.Word=0x58;
    ADCDLY.Word=0xff;
    return true;
}

void __fastcall IOADConverter::SaveState(StateFiler& filer) const
{
    filer.Write('ADCV');
    filer.Write(ADCCON.Word);
    filer.Write(ADCTSC.Word);
    filer.Write(ADCDLY.Word);
    filer.Write(ADCDAT0.Word);
    filer.Write(ADCDAT1.Word);
    filer.Write(SampleX);
    filer.Write(SampleY);
}

void __fastcall IOADConverter::RestoreState(StateFiler& filer)
{
    filer.Verify('ADCV');
    filer.Read(ADCCON.Word);
    filer.Read(ADCTSC.Word);
    filer.Read(ADCDLY.Word);
    filer.Read(ADCDAT0.Word);
    filer.Read(ADCDAT1.Word);
    filer.Read(SampleX);
    filer.Read(SampleY);
}

unsigned __int32 __fastcall IOADConverter::ReadWord(unsigned __int32 IOAddress)
{
    switch (IOAddress) {
    case 0:
        return ADCCON.Word;

    case 4:
        return ADCTSC.Word;

    case 8:
        return ADCDLY.Word;

    case 0xc:
        return ADCDAT0.Word;

    case 0x10:
        return ADCDAT1.Word;

    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        return 0;
    }
}

void __fastcall IOADConverter::WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value)
{
    switch (IOAddress) {
    case 0:
        ADCCON.Word=Value;
        if (ADCCON.Bits.ENABLE_START) {
            // Start auto conversion.  Update the ADCDAT registers with the latest Win32 pen sample data
            // and signal the auto conversion has finished.
            ADCCON.Bits.ENABLE_START=0;
            ADCCON.Bits.ECFLG=1;
            ADCDAT0.Bits.XPDATA=SampleY; // note: XPDATA really does contain the Y sample
            ADCDAT1.Bits.YPDATA=SampleX; // note: YPDATA really does contain the X sample
        }
        ASSERT(ADCCON.Bits.Reserved1 == 0);
        break;

    case 4:
        ADCTSC.Word=Value;
        ASSERT(ADCTSC.Bits.Reserved1 == 0);
        break;

    case 8:
        ADCDLY.Word=Value;
        ASSERT(ADCDLY.Bits.Reserved1 == 0);
        break;

    case 0xc:    // ADCDAT0
    case 0x10:    // ADCDAT1
        break;  // these registers are read-only

    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        break;
    }
}

void IOADConverter::SetPenState(IOADConverter::PenState State)
{
    EnterCriticalSection(&IOLock);

    ADCDAT0.Bits.XY_PST = ADCTSC.Bits.XY_PST;
    ADCDAT0.Bits.UPDOWN = ADCDAT1.Bits.UPDOWN = State;

    LeaveCriticalSection(&IOLock);
}


void IOADConverter::RaiseTCInterrupt(void)
{
    EnterCriticalSection(&IOLock);
    // Signal the CPU that a TC sub-interrupt is pending
    InterruptController.RaiseInterrupt(IOInterruptController::SubSourceINT_TC);
    LeaveCriticalSection(&IOLock);
}

void IOADConverter::SetPenSample(WORD x, WORD y)
{
    if (ADCDAT0.Bits.XY_PST && ADCDAT0.Bits.UPDOWN==0) {
        // Scale the sample from the Win32 window coordinates to 0...1023 to match the
        // ADConverter's 10-bit sample size.
        double ScreenX = (double)LCDController.ScreenX();
        double ScreenY = (double)LCDController.ScreenY();

        // The actual scaling isn't truly 0...1023.  The following values
        // from from empirical testing of WinCE 4.2 on the emulator.

        // SampleX should be between 100 and 965
        SampleX=90+(unsigned __int16)((double)x*(875.0/ScreenX));

        // SampleY should be between 920 and 50
        SampleY=920-(unsigned __int16)((double)y*(870.0/ScreenY));
    }
}

bool __fastcall IOCS8900IO::PowerOn(void)
{
    if (!Configuration.NetworkingEnabled) {
        return true;
    }

    // Check if need to prompt the user before powering on this device
    if ( !Configuration.NoSecurityPrompt && 
        !PromptToAllow( ID_MESSAGE_ENABLE_NETWORKING, L"CS8900" )) {
        Configuration.NetworkingEnabled = false;
        return true;
    }

    TransmitCallback.lpRoutine = TransmitCompletionRoutineStatic;
    TransmitCallback.lpParameter = this;
    ReceiveCallback.lpRoutine = ReceiveCompletionRoutineStatic;
    ReceiveCallback.lpParameter = this;

    if (!VPCNet.PowerOn(MacAddress, 
                        L"CS8900", 
                        (unsigned __int8 *)&Configuration.SuggestedAdapterMacAddressCS8900,
                        &TransmitCallback,
                        &ReceiveCallback)) {
        return false;
    }

    // This event (hCS8900PacketArrived) marks that we received a packet
    hCS8900PacketArrived = CreateEvent(NULL, FALSE, FALSE, NULL); // Auto-reset, initially unsignalled
    if (hCS8900PacketArrived == NULL) {
        return false;
    }
    // This event (hCS8900PacketSent) marks thats a pack transmission has finished
    hCS8900PacketSent = CreateEvent(NULL, FALSE, FALSE, NULL); // Auto-reset, initially unsignalled
    if (hCS8900PacketSent == NULL) {
        return false;
    }
    // Set up the config data structure that EBOOT.NB0 reads, so the user doesn't have to
    // explicitly configure the MAC address each time.
    CSSROMBank0.InitializeEbootConfig(MacAddress);

    BUS_ST = 0x100; // BUS_ST_RDY_4_TX_NOW... TransmitPkt polls until this is set, then sends the packet

    BeginAsyncReceive();

    return true;
}

void __fastcall IOCS8900IO::SaveState(StateFiler& filer) const
{
    filer.Write('NETW');
    filer.Write(IO_RX_TX_DATA_1);
    filer.Write(IO_TX_CMD);
    filer.Write(IO_TX_LENGTH);
    filer.Write(IO_ISQ);
    filer.Write(IO_PACKET_PAGE_POINTER);
    filer.Write(IO_PACKET_PAGE_DATA_0);
    filer.Write(IO_PACKET_PAGE_DATA_1);
    filer.Write(MEMORY_BASE_ADDR.Word);
    filer.Write(MacAddress);
    filer.Write(PKTPG_LOGICAL_ADDR_FILTER);
    filer.Write(LINE_CTL.HalfWord);
    filer.Write(RX_CTL.HalfWord);
    filer.Write(BUS_CTL.HalfWord);
    filer.Write(cbTxBuffer);
    filer.Write(TxBuffer);
    filer.Write(cbRxBuffer);
    int __w64 iCurrentRx=reinterpret_cast<int __w64>(&RxBuffer)-reinterpret_cast<int __w64>(pCurrentRx);
    filer.Write(iCurrentRx);
    filer.Write(RxBuffer);
}

void __fastcall IOCS8900IO::RestoreState(StateFiler& filer)
{
    filer.Verify('NETW');
    filer.Read(IO_RX_TX_DATA_1);
    filer.Read(IO_TX_CMD);
    filer.Read(IO_TX_LENGTH);
    if (IO_TX_LENGTH > sizeof(TxBuffer)) {
        // Security: don't allow IO_TX_LENGTH to point outside of the TxBuffer
        IO_TX_LENGTH = 0;
    }
    filer.Read(IO_ISQ);
    IO_ISQ=0;  // Force the ISQ to its reset value.
    filer.Read(IO_PACKET_PAGE_POINTER);
    filer.Read(IO_PACKET_PAGE_DATA_0);
    filer.Read(IO_PACKET_PAGE_DATA_1);
    filer.Read(MEMORY_BASE_ADDR.Word);
    filer.Read(MacAddress);
    filer.Read(PKTPG_LOGICAL_ADDR_FILTER);
    filer.Read(LINE_CTL.HalfWord);
    filer.Read(RX_CTL.HalfWord);
    filer.Read(BUS_CTL.HalfWord);
    filer.Read(cbTxBuffer);
    if (cbTxBuffer > sizeof(TxBuffer)) {
        // Security: don't allow cbTxBuffer to point outside of the TxBuffer
        cbTxBuffer = 0;
    }
    filer.Read(TxBuffer);
    filer.Read(cbRxBuffer);
    if (cbRxBuffer > sizeof(RxBuffer)) {
        // Security: don't allow cbRxBuffer to point outside of the RxBuffer
        cbRxBuffer = 0;
    }
    unsigned int __w64 iCurrentRx;
    filer.Read(iCurrentRx);
    if (iCurrentRx >= sizeof(RxBuffer)) {
        // Security: don't allow iCurrentRx to point outside of the RxBuffer
        iCurrentRx = 0;
    }
    pCurrentRx=reinterpret_cast<unsigned __int16*>(reinterpret_cast<unsigned __int8*>(&RxBuffer)+iCurrentRx);
    filer.Read(RxBuffer);
}

unsigned __int16 __fastcall IOCS8900Memory::ReadHalf(unsigned __int32 IOAddress)
{
    switch (IOAddress) {
    case 0:    // PKTPTG_EISA_NUMBER
        return 0x630e; // CS8900_EISA_NUMBER

    case 2: // PKTPG_PRDCT_IO_CODE
        return 0;  // This value AND with 0xe0ff (CS8900_PRDCT_ID_MASK) must be 0x0000 (CS8900_PRDCT_ID).

    case 0x104: // PKTPG_RX_CTL
        return CS8900IO.RX_CTL.HalfWord;

    case 0x112: // PKTPG_LINE_CTL
        return CS8900IO.LINE_CTL.HalfWord;

    case 0x116: // PKTPG_BUS_CTL
        return CS8900IO.BUS_CTL.HalfWord;

    case 0x136: // PKTPG_SELF_ST
        return 0x80;  // SELF_ST_INITD set, SELF_ST_SIBUSY cleared, to satisify Reset()

    case 0x138: // PKTPG_BUS_ST
        if ( CS8900IO.BUS_ST == 0 && Configuration.NetworkingEnabled &&
             !CpuAreInterruptsEnabled() ) // We are currently busy and are executing in
                                          // single threaded environment
        {
            HANDLE Handles[2];
            Handles[0] = CS8900IO.hCS8900PacketSent;     // packet has been sent
            Handles[1] = hIdleEvent;            // CPU interrupt pending
            LeaveCriticalSection(&IOLock);
            WaitForMultipleObjects(2, Handles, FALSE, 1);
            EnterCriticalSection(&IOLock);
        }
        return CS8900IO.BUS_ST;

    // The following are write-only registers
    case 0x2c:  // PKTPG_MEMORY_BASE_ADDR
    case 0x2e:  // PKTPG_MEMORY_BASE_ADDR+2
    case 0x102: // PKTPG_RX_CFG
    case 0x106: // PKTPG_TX_CFG
    case 0x114: // PKTPG_SELF_CTL
    case 0x150: // PKTPG_LOGICAL_ADDR_FILTER
        return CS8900IO.PKTPG_LOGICAL_ADDR_FILTER[0];
    case 0x152: // PKTPG_LOGICAL_ADDR_FILTER+2
        return CS8900IO.PKTPG_LOGICAL_ADDR_FILTER[1];
    case 0x154: // PKTPG_LOGICAL_ADDR_FILTER+4
        return CS8900IO.PKTPG_LOGICAL_ADDR_FILTER[2];
    case 0x156: // PKTPG_LOGICAL_ADDR_FILTER+6
        return CS8900IO.PKTPG_LOGICAL_ADDR_FILTER[3];
    case 0x158: // PKTPG_INDIVISUAL_ADDR
    case 0x15a: // PKTPG_INDIVISUAL_ADDR+2
    case 0x15c: // PKTPG_INDIVISUAL_ADDR+4
    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        return 0;
    }
}

void __fastcall IOCS8900Memory::WriteHalf(unsigned __int32 IOAddress, unsigned __int16 Value)
{
    switch (IOAddress) {
    case 0x22:  // PKTPG_INTERRUPT_NUMBER
        if (Value != 0) TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
                // According to WinCE, "Interrupt request will be generated from INTRQ0 pin"
                // armint.c indicates the CS8900 interrupts on EINT9.
        break;

    case 0x2c:  // PKTPG_MEMORY_BASE_ADDR
        CS8900IO.MEMORY_BASE_ADDR.HalfWords.LowHalf = Value;
        break;

    case 0x2e:  // PKTPG_MEMORY_BASE_ADDR+2
        CS8900IO.MEMORY_BASE_ADDR.HalfWords.HighHalf = Value;
        break;

    case 0x102: // PKTPG_RX_CFG
        break;  // set to RX_CFG_RX_OK_I_E|RX_CFG_LOW_BITS during initialization

    case 0x104: // PKTPG_RX_CTL
        // set to RX_CTL_RX_OK_A|RX_CTL_IND_ADDR_A|RX_CTL_BROADCASTR_A|RX_CTL|LOW_BITS during initialization
        CS8900IO.RX_CTL.HalfWord = Value;
        break;    

    case 0x106: // PKTPG_TX_CFG
        break;  // set to TX_CFG_LOW_BITS during initialization

    case 0x112: // PKTPG_LINE_CTL
        // set to LINE_CTL_10_BASE_T|LINE_CTL_MOD_BACKOFF during initialization
        // then later, LINE_CTL_RX_ON|LINE_CTL_TX_ON are ORed in, completing initialization.
        // RXON is cleared in order to enable receipt of a frame.
        CS8900IO.LINE_CTL.HalfWord=Value;
        break;  

    case 0x114:    // PKTPG_SELF_CTL
        // See the SELF_* defines in cs8900dbg.h:
        // 0x15 == SELF_CTL_LOW_BITS
        // 0x40 == SELF_CTL_RESET     - set during startup, to reset the NIC
        // 0x80 == SELF_ST_INITD
        // 0x100== SELF_ST_SIBUSY
        break;

    case 0x116: // PKTPG_BUS_CTL
        // Check if we need to trigger an interrupt for a current packet
        if ( CS8900IO.IO_ISQ == 0x0304 && !CS8900IO.BUS_CTL.Bits.EnableIRQ 
            && (Value & 0x8000) ) {
            GPIO.RaiseInterrupt(9); // EINT9 is reserved for the CS8900
        }
        // Set to BUS_CTL_MEMORY_E|BUS_CTL_LOW_BITS during initialization.
        // Later, BUS_CTL_ENABLE_IRQ is set and cleared to control interrupt-driven I/O
        CS8900IO.BUS_CTL.HalfWord=Value;
        break;

    case 0x138: // PKTPG_BUS_ST
        break;  // read-only register

    case 0x150: // PKTPG_LOGICAL_ADDR_FILTER
        CS8900IO.PKTPG_LOGICAL_ADDR_FILTER[0] = Value;
        break;
    case 0x152:
        CS8900IO.PKTPG_LOGICAL_ADDR_FILTER[1] = Value;
        break;
    case 0x154:
        CS8900IO.PKTPG_LOGICAL_ADDR_FILTER[2] = Value;
        break;
    case 0x156:
        CS8900IO.PKTPG_LOGICAL_ADDR_FILTER[3] = Value;
        break;  

    case 0x158: // PKTPG_INDIVISUAL_ADDR
        CS8900IO.MacAddress[0]=Value;
        break;

    case 0x15a: // PKTPG_INDIVISUAL_ADDR+2
        CS8900IO.MacAddress[1]=Value;
        break;

    case 0x15c: // PKTPG_INDIVISUAL_ADDR+4
        CS8900IO.MacAddress[2]=Value;
        break;

    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE); // unknown PKTPG_ constant - see cs8900dbg.h
        break;
    }
}

unsigned __int16 __fastcall IOCS8900IO::ReadHalf(unsigned __int32 IOAddress)
{
    switch (IOAddress) {
    case 0:
        if (cbRxBuffer) {
            unsigned __int16 Value;

            if (cbRxBuffer == 1) {
                Value = *(unsigned __int8*)pCurrentRx;
                cbRxBuffer=0;
            } else {
                Value = *pCurrentRx;
                pCurrentRx++;
                cbRxBuffer-=sizeof(unsigned __int16);
            }
            if (cbRxBuffer == 0) {
                IO_ISQ=0; // clear the 'packet ready' flags.
                BeginAsyncReceive();
                GPIO.ClearInterrupt(9); // EINT9 is reserved for the CS8900
            }
            return Value;
        }
        return 0;

    case 2:
        return IO_RX_TX_DATA_1;

    case 4:
        return IO_TX_CMD;

    case 6:
        return IO_TX_LENGTH;

    case 8:
        // Check if the IRQs are disabled and this is may be polling code
        // non-polling reads will incur a 1ms penalty in non IRQ mode
        if ( IO_ISQ == 0 && (!BUS_CTL.Bits.EnableIRQ || !CpuAreInterruptsEnabled()) 
             && Configuration.NetworkingEnabled)
        {
            HANDLE Handles[2];
            Handles[0] = hCS8900PacketArrived;  // packet has arrived
            Handles[1] = hIdleEvent;            // CPU interrupt pending
            LeaveCriticalSection(&IOLock);
            WaitForMultipleObjects(2, Handles, FALSE, 1);
            EnterCriticalSection(&IOLock);
        }
        return IO_ISQ;

    case 10:
        return IO_PACKET_PAGE_POINTER;

    case 12:
        return CS8900Memory.ReadHalf(IO_PACKET_PAGE_POINTER);

    case 14:
        return IO_PACKET_PAGE_DATA_1;

    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        return 0;
    }
}

void __fastcall IOCS8900IO::WriteHalf(unsigned __int32 IOAddress, unsigned __int16 Value)
{
    switch (IOAddress) {
    case 0:
        // Packets are transmitted by setting IO_TX_LENGTH to the number of bytes, then
        // for each byte, writing it to IO_RX_TX_DATA_0.
        if (IO_TX_LENGTH == 1) {
            TxBuffer[cbTxBuffer] = (unsigned __int8)Value;
            cbTxBuffer++;
            IO_TX_LENGTH=0;
        } else {
            *(unsigned __int16*)&TxBuffer[cbTxBuffer] = Value;
            cbTxBuffer+=sizeof(Value);
            IO_TX_LENGTH-=sizeof(unsigned __int16);
        }
        if (IO_TX_LENGTH == 0) {
            TransmitPacket();
        }
        break;

    case 2:
        IO_RX_TX_DATA_1=Value;
        break;

    case 4:
        IO_TX_CMD=Value;
        break;

    case 6:
        if (Value >= sizeof(TxBuffer)) {
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        }
        IO_TX_LENGTH=Value;
        cbTxBuffer=0;
        break;

    case 8:
        IO_ISQ=Value;
        break;

    case 10:
        IO_PACKET_PAGE_POINTER=Value;
        break;

    case 12:    // IO_PACKET_PAGE_DATA_0
        CS8900Memory.WriteHalf(IO_PACKET_PAGE_POINTER, Value);
        break;

    case 14:
        IO_PACKET_PAGE_DATA_1=Value;
        break;

    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        break;
    }
}

inline bool __fastcall IOCS8900IO::ShouldIndicatePacketToGuest(const EthernetHeader & inEthernetHeader)
{
    // Make sure we are in perfect filtering mode.
    ASSERT(inEthernetHeader.fDestinationAddress.IsMulticast());

    if (inEthernetHeader.fDestinationAddress.IsBroadcast())     // Is this a broadcast packet?    
        // Filter out broadcast packets sent from ourselves.  Otherwise, EBOOT.NB0 sends
        // out an ARP to confirm that the DHCP-assigned IP address isn't in use by some
        // other machine.  When the ARP request arrives at the emulator, EBOOT sends
        // back a reply via a broadcast... to itself.  When this happens, EBOOT thinks
        // another device owns the IP address and aborts.
        if (inEthernetHeader.fSourceAddress.IsEqualTo((unsigned __int8 *)MacAddress))        
            return false;
        else
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
    unsigned __int8 * NIC_MC_ADDR = (unsigned __int8 *)CS8900IO.PKTPG_LOGICAL_ADDR_FILTER;
    return (( ( (NIC_MC_ADDR[crc >> 3]) >> (crc & 7) ) & 0x01) != 0);
}

void IOCS8900IO::BeginAsyncReceive(void)
{
    // The first __int16 in the RxBuffer is status, and is unused by WinCE
    RxBuffer.RxStatus=0;

    if (VPCNet.BeginAsyncReceivePacket(RxBuffer.Buffer, sizeof(RxBuffer.Buffer)) == false) {
        ASSERT(FALSE);
    }
}

void __fastcall IOCS8900IO::ReceiveCompletionRoutineStatic(LPVOID lpParameter, DWORD dwBytesTransferred, LPOVERLAPPED lpOverlapped)
{
    IOCS8900IO *pThis = (IOCS8900IO *)lpParameter;

    return pThis->ReceiveCompletionRoutine(dwBytesTransferred);
}

void __fastcall IOCS8900IO::ReceiveCompletionRoutine(DWORD dwBytesTransferred)
{
    RxBuffer.FrameLength = (unsigned __int16)dwBytesTransferred;

    // Filter out multicase traffic according to the filter register
    EthernetHeader & enetDatagram = *reinterpret_cast<EthernetHeader *>(RxBuffer.Buffer);
    if (enetDatagram.fDestinationAddress.IsMulticast() && !ShouldIndicatePacketToGuest(enetDatagram)) {
        // This is a broadcast to ourselves - drop it and initiate another receive
        BeginAsyncReceive();
    } else {
        // Prepare to spool the packet into WinCE
        pCurrentRx=(unsigned __int16*)&RxBuffer;

        // Report the data as available and prepare to return it back
        EnterCriticalSection(&IOLock);
        IO_ISQ = 0x0304; // RX_EVENT_HASHED|RX_EVENT_RX_OK|REG_NUM_RX_EVENT
        if (BUS_CTL.Bits.EnableIRQ) {
            GPIO.RaiseInterrupt(9); // EINT9 is reserved for the CS8900
        }
        else
            SetEvent(hCS8900PacketArrived);
        cbRxBuffer = RxBuffer.FrameLength+2*sizeof(__int16);
        LeaveCriticalSection(&IOLock);
    }
}

// A network packet is queued up in TxBuffer/cbTxBuffer.  Send it over the network.
void __fastcall IOCS8900IO::TransmitPacket(void)
{
    ASSERT(BUS_ST == 0x100);

    if (!Configuration.NetworkingEnabled) {
        // Silently ignore the transmission - networking isn't enabled
        TransmitCompletionRoutineStatic(this, 0, NULL);
        return;
    }

    BUS_ST = 0; // indicate that we're busy transmitting a packet

    if (VPCNet.BeginAsyncTransmitPacket(cbTxBuffer, TxBuffer) == false) {
        ASSERT(FALSE);
    }
}

void IOCS8900IO::TransmitCompletionRoutineStatic(LPVOID lpParameter, DWORD dwBytesTransferred, LPOVERLAPPED lpOverlapped)
{
    IOCS8900IO *pThis = (IOCS8900IO *)lpParameter;

    EnterCriticalSection(&IOLock);
    ASSERT(pThis->BUS_ST == 0);
    SetEvent(pThis->hCS8900PacketSent);
    pThis->BUS_ST = 0x100; // BUS_ST_RDY_4_TX_NOW... TransmitPkt polls until this is set, then sends the packet
    LeaveCriticalSection(&IOLock);
}

bool __fastcall IOMAPPEDMEMPCMCIA::PowerOn(void)
{
    pDevice = &PCMCIADefaultDevice;
    return true;
}

unsigned __int8 __fastcall IOMAPPEDMEMPCMCIA::ReadByte(unsigned __int32 IOAddress)
{
    return pDevice->ReadMemoryByte(IOAddress);

}

unsigned __int16 __fastcall IOMAPPEDMEMPCMCIA::ReadHalf(unsigned __int32 IOAddress)
{
    return pDevice->ReadMemoryHalf(IOAddress);
}

void __fastcall IOMAPPEDMEMPCMCIA::WriteByte(unsigned __int32 IOAddress, unsigned __int8 Value) 
{
    return pDevice->WriteMemoryByte(IOAddress, Value);
}

void __fastcall IOMAPPEDMEMPCMCIA::WriteHalf(unsigned __int32 IOAddress, unsigned __int16 Value) 
{
    
    return pDevice->WriteMemoryHalf(IOAddress, Value);

}

bool __fastcall IOMAPPEDIOPCMCIA::PowerOn(void)
{
    pDevice = &PCMCIADefaultDevice;
    return true;
}

unsigned __int8 __fastcall IOMAPPEDIOPCMCIA::ReadByte(unsigned __int32 IOAddress)
{
    unsigned __int32 IOAddressOffset = IOAddress-0x0FF0000;

    if (CONTROLPCMCIA.REG_WINDOW_ENABLE.Bits.WIN_IO_MAP1_ENABLE 
        && IOAddressOffset >= CONTROLPCMCIA.REG_IO_MAP1_START_ADDR.HalfWord 
        && IOAddressOffset <= CONTROLPCMCIA.REG_IO_MAP1_END_ADDR.HalfWord){
        
        //The address is within a PCMCIA card plugged into a slot
        return pDevice->ReadByte((IOAddressOffset - CONTROLPCMCIA.REG_IO_MAP1_START_ADDR.HalfWord));
    }

    else if (CONTROLPCMCIA.REG_WINDOW_ENABLE.Bits.WIN_IO_MAP0_ENABLE 
        && IOAddressOffset >= CONTROLPCMCIA.REG_IO_MAP0_START_ADDR.HalfWord 
        && IOAddressOffset <= CONTROLPCMCIA.REG_IO_MAP0_END_ADDR.HalfWord)     
    {
        //The address is within a PCMCIA card plugged into a slot
        return pDevice->ReadByte((IOAddressOffset - CONTROLPCMCIA.REG_IO_MAP0_START_ADDR.HalfWord));
    }

    else {
    // The address is within PCMCIA space but isn't a known address
        return DefaultDevice.ReadByte(IOAddress);
    }
}

unsigned __int16 __fastcall IOMAPPEDIOPCMCIA::ReadHalf(unsigned __int32 IOAddress)
{
    unsigned __int32 IOAddressOffset = IOAddress-0x0FF0000;

    if (CONTROLPCMCIA.REG_WINDOW_ENABLE.Bits.WIN_IO_MAP1_ENABLE 
        && IOAddressOffset >= CONTROLPCMCIA.REG_IO_MAP1_START_ADDR.HalfWord 
        && IOAddressOffset <= CONTROLPCMCIA.REG_IO_MAP1_END_ADDR.HalfWord){
        
        //The address is within a PCMCIA card plugged into a slot
        return pDevice->ReadHalf((IOAddressOffset - CONTROLPCMCIA.REG_IO_MAP1_START_ADDR.HalfWord));
    }    
    
    else if (CONTROLPCMCIA.REG_WINDOW_ENABLE.Bits.WIN_IO_MAP0_ENABLE 
        && IOAddressOffset >= CONTROLPCMCIA.REG_IO_MAP0_START_ADDR.HalfWord 
        && IOAddressOffset <= CONTROLPCMCIA.REG_IO_MAP0_END_ADDR.HalfWord)     
    {
        //The address is within a PCMCIA card plugged into a slot
        return pDevice->ReadHalf((IOAddressOffset - CONTROLPCMCIA.REG_IO_MAP0_START_ADDR.HalfWord));
    }

    else {
    // The address is within PCMCIA space but isn't a known address
        return DefaultDevice.ReadHalf(IOAddress);
    }
}

void __fastcall IOMAPPEDIOPCMCIA::WriteByte(unsigned __int32 IOAddress, unsigned __int8 Value) 
{
    unsigned __int32 IOAddressOffset = IOAddress-0x0FF0000;

    if (CONTROLPCMCIA.REG_WINDOW_ENABLE.Bits.WIN_IO_MAP1_ENABLE 
        && IOAddressOffset >= CONTROLPCMCIA.REG_IO_MAP1_START_ADDR.HalfWord 
        && IOAddressOffset <= CONTROLPCMCIA.REG_IO_MAP1_END_ADDR.HalfWord){
        
        //The address is within a PCMCIA card plugged into a slot
        pDevice->WriteByte((IOAddressOffset - CONTROLPCMCIA.REG_IO_MAP1_START_ADDR.HalfWord), Value);
    }

    else if (CONTROLPCMCIA.REG_WINDOW_ENABLE.Bits.WIN_IO_MAP0_ENABLE 
        && IOAddressOffset >= CONTROLPCMCIA.REG_IO_MAP0_START_ADDR.HalfWord 
        && IOAddressOffset <= CONTROLPCMCIA.REG_IO_MAP0_END_ADDR.HalfWord)     
    {
        //The address is within a PCMCIA card plugged into a slot
        pDevice->WriteByte((IOAddressOffset - CONTROLPCMCIA.REG_IO_MAP0_START_ADDR.HalfWord), Value);
    }

    else {
    // The address is within PCMCIA space but isn't a known address
        DefaultDevice.WriteByte(IOAddress, Value);
    }
}

void __fastcall IOMAPPEDIOPCMCIA::WriteHalf(unsigned __int32 IOAddress, unsigned __int16 Value) 
{
    unsigned __int32 IOAddressOffset = IOAddress-0x0FF0000;

    if (CONTROLPCMCIA.pDevice && CONTROLPCMCIA.REG_WINDOW_ENABLE.Bits.WIN_IO_MAP1_ENABLE 
        && IOAddressOffset >= CONTROLPCMCIA.REG_IO_MAP1_START_ADDR.HalfWord 
        && IOAddressOffset <= CONTROLPCMCIA.REG_IO_MAP1_END_ADDR.HalfWord){
        
        //The address is within a PCMCIA card plugged into a slot
        pDevice->WriteHalf((IOAddressOffset - CONTROLPCMCIA.REG_IO_MAP1_START_ADDR.HalfWord), Value);
    }

    else if (CONTROLPCMCIA.REG_WINDOW_ENABLE.Bits.WIN_IO_MAP0_ENABLE 
        && IOAddressOffset >= CONTROLPCMCIA.REG_IO_MAP0_START_ADDR.HalfWord 
        && IOAddressOffset <= CONTROLPCMCIA.REG_IO_MAP0_END_ADDR.HalfWord)     
    {
        //The address is within a PCMCIA card plugged into a slot
        pDevice->WriteHalf((IOAddressOffset - CONTROLPCMCIA.REG_IO_MAP0_START_ADDR.HalfWord), Value);
    }

    else {
    // The address is within PCMCIA space but isn't a known address
        DefaultDevice.WriteHalf(IOAddress, Value);
    }
}

bool __fastcall IOCONTROLPCMCIA::PowerOn(void)
{
    if ( Configuration.PCMCIACardInserted &&
         !Configuration.NoSecurityPrompt && 
         !PromptToAllow( ID_MESSAGE_ENABLE_NETWORKING, L"NE2000" ) )
    {
        // Disable foldersharing in the configuration object by clearing the path
        Configuration.PCMCIACardInserted = false;
    }
    if (Configuration.PCMCIACardInserted) {
        InsertCard(&NE2000);
    }
    REG_CHIP_INFO=0xc0;
    return true;
}

bool __fastcall IOCONTROLPCMCIA::Reconfigure(__in_z const wchar_t * NewParam)
{
	if (wcscmp(NewParam, L"0") == 0) {
        RemoveCard();
    } else if (wcscmp(NewParam, L"1") == 0) {
        InsertCard(&NE2000);
    } else {
        return false;
    }
    return true;
}


unsigned __int8 __fastcall IOCONTROLPCMCIA::ReadByte(unsigned __int32 IOAddress)
{
    if (IOAddress >= 0 && IOAddress <= 1) {
        // The address is within the PD6710 PCMCIA controller itself
        switch (IOAddress) {
        case 0: // PD6710_INDEX
            return INDEX;

        case 1: // PD6710_DATA
            switch (INDEX) {
            case 0x00:        // REG_CHIP_REVISION
                return 0x83;    // pcmcia.c expects this revision

            case 0x01:        // REG_INTERFACE_STATUS
                if (IsCardInserted()) {
                    // The card is inserted
                    REG_INTERFACE_STATUS.Bits.STS_CD1=1;
                    REG_INTERFACE_STATUS.Bits.STS_CD2=1;
                    if ((REG_POWER_CONTROL & 0x90) == 0x90) {
                        // The card is powered on, too.  Report it as ready, as the
                        // driver powers the card on and spins until the ready bit
                        // becomes set.
                        REG_INTERFACE_STATUS.Bits.STS_CARD_READY=1;
                    } else {
                        // The card is powered off
                        REG_INTERFACE_STATUS.Bits.STS_CARD_READY=0;
                    }
                } else {
                    // No card present
                    REG_INTERFACE_STATUS.Bits.STS_CD1=0;
                    REG_INTERFACE_STATUS.Bits.STS_CD2=0;
                    REG_INTERFACE_STATUS.Bits.STS_CARD_READY=0;
                }
                return REG_INTERFACE_STATUS.Byte;

            case 0x02:        // REG_POWER_CONTROL
                return REG_POWER_CONTROL;

            case 0x03:        // REG_INTERRUPT_AND_GENERAL_CONTROL
                return REG_INTERRUPT_AND_GENERAL_CONTROL.Byte;

            case 0x04:        // REG_CARD_STATUS_CHANGE
                {
                    // Reading from the register clears all of the status bits
                    unsigned __int8 Value = REG_CARD_STATUS_CHANGE;
                    REG_CARD_STATUS_CHANGE=0;
                    return Value;
                }

            case 0x05:        // REG_STATUS_CHANGE_INT_CONFIG
                return REG_STATUS_CHANGE_INT_CONFIG.Byte;

            case 0x06:        // REG_WINDOW_ENABLE
                return REG_WINDOW_ENABLE.Byte;

            case 0x07:        // REG_IO_WINDOW_CONTROL
                return REG_IO_WINDOW_CONTROL;

            case 0x10:        // REG_MEM_MAP0_START_ADDR_LO    - 0x00
                return 0;

            case 0x11:        // REG_MEM_MAP0_START_ADDR_HI    - 0x00
                return 0;

            case 0x16:        // REG_GENERAL_CONTROL
                return 1;   // report the card as 5.0V so pcmcia.sys treats it as a 16-bit card.

            case 0x17:        // REG_FIFO_CONTROL:
                return 0;

            case 0x18:        // REG_MEM_MAP1_START_ADDR_LO
                return 0;

            case 0x19:        // REG_MEM_MAP1_START_ADDR_HI
                return 0;

            case 0x1e:        // REG_GLOBAL_CONTROL
                return 0;

            case 0x1f:        // REG_CHIP_INFO
                {
                    // The first read after a reset or write to this register returns '11' in the top two
                    // bits.  The second (and subsequent) reads return '00' in the top two bits.
                    unsigned __int8 Value = REG_CHIP_INFO;
                    REG_CHIP_INFO &= ~0xc0;
                    return Value;
                
                }

            case 0x20:        // REG_MEM_MAP2_START_ADDR_LO
                return 0;

            case 0x21:        // REG_MEM_MAP2_START_ADDR_HI
                return 0;

            case 0x28:        // REG_MEM_MAP3_START_ADDR_HI
                return 0;

            case 0x29:        // REG_MEM_MAP3_START_ADDR_HI
                return 0;

            case 0x30:        // REG_MEM_MAP4_START_ADDR_LO
                return 0;

            case 0x31:        // REG_MEM_MAP4_START_ADDR_HI
                return 0;

            default:
                TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
                return 0;
            }

        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            return 0;
        }
    } else if (MapIOAddress(&IOAddress)) {
        return pDevice->ReadByte(IOAddress);
    }

    // Else the address is within PCMCIA space but isn't a known address.  Memory windows
    // are as follows (must add 0x10000000 to see the true address):
    // PCMCIA_MEM_WIN_BASE0        0000 - 1fff
    // PCMCIA_MEM_WIN_BASE1     2000 - 3fff
    // PCMCIA_MEM_WIN_BASE2     4000 - 4fff
    // PCMCIA_MEM_WIN_BASE3     6000 - 6fff
    // addresses 0x11000000 - 0x1100ffff are I/O window 0, which includes the PD6710 registers
    return DefaultDevice.ReadByte(IOAddress);
}

unsigned __int16 __fastcall IOCONTROLPCMCIA::ReadHalf(unsigned __int32 IOAddress)
{
    if (IOAddress >= 0 && IOAddress <= 1) {
        // The address is within the PD6710 PCMCIA controller itself
        return DefaultDevice.ReadHalf(IOAddress);
    } else if (MapIOAddress(&IOAddress)) {
        return pDevice->ReadHalf(IOAddress);
    } else {
        // The address is within PCMCIA space but isn't a known address
        return DefaultDevice.ReadHalf(IOAddress);
    }
}

void __fastcall IOCONTROLPCMCIA::WriteByte(unsigned __int32 IOAddress, unsigned __int8 Value) {
    if (IOAddress >= 0 && IOAddress <= 1) {
        // The address is within the PD6710 PCMCIA controller itself
        switch (IOAddress) {
        case 0: // PD6710_INDEX
            INDEX = Value;
            break;

        case 1: //PD6710_DATA
            switch (INDEX) {
            case 0x02:        // REG_POWER_CONTROL
                if (IsCardInserted()) {
                    if ((Value & 0x90) != (REG_POWER_CONTROL & 0x90)) {
                        // The combination of PWR_OUTPUT_ENABLE and PWR_VCC_POWER are changing
                        if ((Value & 0x90) == 0x90) {
                            // The new setting has both PWR_OUTPUT_ENABLE and PWR_VCC_POWER set.
                            // Power on the device.
                            if (!pDevice->PowerOn()) {
                                // Oop - power-on failed, likely due to a lack of Win32 resources.
                                // Do our best here, by simulating an unplug of the card.
                                RemoveCard();
                            }
                            else
                            {
                                //PowerOn Successful.  Set device pointer of Memory and 
                                //IO PCMCIA Window instances to pDevice
                                MAPPEDIOPCMCIA.pDevice = pDevice;
                                MAPPEDMEMPCMCIA.pDevice = pDevice;
                            }
                        } else if ((REG_POWER_CONTROL & 0x90) == 0x90) {
                            // The new setting has one or the other or both clear and the
                            // previous setting had the device powered on.  Power off
                            // the device.
                            pDevice->PowerOff();
                            MAPPEDIOPCMCIA.pDevice = &PCMCIADefaultDevice;
                            MAPPEDMEMPCMCIA.pDevice = &PCMCIADefaultDevice;
                        }
                    }
                }
                REG_POWER_CONTROL=Value;
                break;

            case 0x03:        // REG_INTERRUPT_AND_GENERAL_CONTROL
                REG_INTERRUPT_AND_GENERAL_CONTROL.Byte = Value;
                ASSERT(REG_INTERRUPT_AND_GENERAL_CONTROL.Bits.CardIRQSelect == 0 ||
                       REG_INTERRUPT_AND_GENERAL_CONTROL.Bits.CardIRQSelect == 3 ||
                       REG_INTERRUPT_AND_GENERAL_CONTROL.Bits.CardIRQSelect == 8);
                break;

            case 0x05:        // REG_STATUS_CHANGE_INT_CONFIG
                REG_STATUS_CHANGE_INT_CONFIG.Byte = Value;
                break;

            case 0x06:        // REG_WINDOW_ENABLE
                REG_WINDOW_ENABLE.Byte = Value;
                break;

            case 0x07:        // REG_IO_WINDOW_CONTROL
                REG_IO_WINDOW_CONTROL = Value;
                break;

            case 0x08:        // REG_IO_MAP0_START_ADDR_LO
                REG_IO_MAP0_START_ADDR.Bytes.LO=Value;        // expect Value==0x00
                break;

            case 0x09:        // REG_IO_MAP0_START_ADDR_HI
                REG_IO_MAP0_START_ADDR.Bytes.HI=Value;        // expect Value==0x03
                break;

            case 0x0a:        // REG_IO_MAP0_END_ADDR_LO
                REG_IO_MAP0_END_ADDR.Bytes.LO=Value;        // expect Value==0x40
                break;

            case 0x0b:        // REG_IO_MAP0_END_ADDR_HI
                REG_IO_MAP0_END_ADDR.Bytes.HI=Value;        // expect Value==0x03, for range 0x300-0x340
                break;

            case 0x0c:        // REG_IO_MAP1_START_ADDR_LO
                REG_IO_MAP1_START_ADDR.Bytes.LO=Value;        // expect Value==0x00
                break;

            case 0x0d:        // REG_IO_MAP1_START_ADDR_HI
                REG_IO_MAP1_START_ADDR.Bytes.HI=Value;        // expect Value==0x03
                break;

            case 0x0e:        // REG_IO_MAP1_END_ADDR_LO
                REG_IO_MAP1_END_ADDR.Bytes.LO=Value;        // expect Value==0x1f
                break;

            case 0x0f:        // REG_IO_MAP1_END_ADDR_HI
                REG_IO_MAP1_END_ADDR.Bytes.HI=Value;        // expect Value==0x03
                break;

            case 0x10:        // REG_MEM_MAP0_START_ADDR_LO    - 0x00
                ASSERT(Value == 0);
                break;

            case 0x11:        // REG_MEM_MAP0_START_ADDR_HI    - 0x00
                ASSERT(Value == 0);
                break;

            case 0x12:        // REG_MEM_MAP0_END_ADDR_LO
                ASSERT(Value == 1 || Value == 0xFF);        // indicates 0...0xffff
                break; 

            case 0x13:        // REG_MEM_MAP0_END_ADDR_HI
                ASSERT((Value & 0x3f) == 0 || (Value & 0x3f) == 7);  // the top two bits are ignored, but the address itself must be 0
                break;

            case 0x14:        // REG_MEM_MAP0_ADDR_OFFSET_LO
                ASSERT(Value == 0);
                break;

            case 0x15:        // REG_MEM_MAP0_ADDR_OFFSET_HI
                ASSERT((Value & 0x3f) == 0); // the top two bits are ignored, but the address itself must be 0
                break;

            case 0x16:        // REG_GENERAL_CONTROL
                break;        // ignore writes - the emulator doesn't need this register

            case 0x17:        // REG_FIFO_CONTROL                - 0x80 (FIFO_EMPTY_WRITE)
                break;        // ignore writes - the emulator doesn't need this register

            case 0x1e:        // REG_GLOBAL_CONTROL
                break;        // ignore writes - the emulator doesn't need this register

            case 0x1f:        // REG_CHIP_INFO
                REG_CHIP_INFO = 0xc0;
                break;

            case 0x18:        // REG_MEM_MAP1_START_ADDR_LO
            case 0x19:        // REG_MEM_MAP1_START_ADDR_HI
            case 0x1a:        // REG_MEM_MAP1_END_ADDR_LO
            case 0x1b:        // REG_MEM_MAP1_END_ADDR_LO
            case 0x1c:        // REG_MEM_MAP1_ADDR_OFFSET_LO
            case 0x1d:        // REG_MEM_MAP1_ADDR_OFFSET_LO
            case 0x20:        // REG_MEM_MAP2_START_ADDR_LO
            case 0x21:        // REG_MEM_MAP2_START_ADDR_HI
            case 0x22:        // REG_MEM_MAP2_END_ADDR_LO
            case 0x23:        // REG_MEM_MAP2_END_ADDR_HI
            case 0x24:        // REG_MEM_MAP2_ADDR_OFFSET_LO
            case 0x25:        // REG_MEM_MAP2_ADDR_OFFSET_HI
            case 0x28:        // REG_MEM_MAP3_START_ADDR_LO
            case 0x29:        // REG_MEM_MAP3_START_ADDR_HI
            case 0x2a:        // REG_MEM_MAP3_END_ADDR_LO
            case 0x2b:        // REG_MEM_MAP3_END_ADDR_HI
            case 0x2c:        // REG_MEM_MAP3_ADDR_OFFSET_LO
            case 0x2d:        // REG_MEM_MAP3_ADDR_OFFSET_HI
            case 0x30:        // REG_MEM_MAP4_START_ADDR_LO
            case 0x31:        // REG_MEM_MAP4_START_ADDR_HI
            case 0x32:        // REG_MEM_MAP4_END_ADDR_LO
            case 0x33:        // REG_MEM_MAP4_END_ADDR_HI
            case 0x34:        // REG_MEM_MAP4_ADDR_OFFSET_LO
            case 0x35:        // REG_MEM_MAP4_ADDR_OFFSET_HI
            case 0x36:      // REG_CARD_IO_MAP0_OFFSET_L    - 0x00
            case 0x37:        // REG_CARD_IO_MAP0_OFFSET_H    - 0x00
            case 0x3a:        // REG_SETUP_TIMING0
            case 0x3b:        // REG_CMD_TIMING0
            case 0x3c:        // REG_RECOVERY_TIMING0
            case 0x3d:        // REG_SETUP_TIMING1
            case 0x3e:        // REG_CMD_TIMING1
            case 0x3f:        // REG_RECOVERY_TIMING1
                break;

            default:
                TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            }
            break;

        default:
            TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
            break;
        }
    } 
    else if (MapIOAddress(&IOAddress)) 
    {
        pDevice->WriteByte(IOAddress, Value);
    }
    
    else 
    {
        // The address is within PCMCIA space but isn't a known address
        DefaultDevice.WriteByte(IOAddress, Value);
    }
}

void __fastcall IOCONTROLPCMCIA::WriteHalf(unsigned __int32 IOAddress, unsigned __int16 Value) {
    if (IOAddress >= 0 && IOAddress <= 1) {
        // The address is within the PD6710 PCMCIA controller itself
        DefaultDevice.WriteHalf(IOAddress, Value);
    } else if (MapIOAddress(&IOAddress)) {
        pDevice->WriteHalf(IOAddress, Value);
    
    } else {
        // The address is within PCMCIA space but isn't a known address
        DefaultDevice.WriteHalf(IOAddress, Value);
    }
}


bool IOCONTROLPCMCIA::Reset(void)
{
    if (IsCardInserted() && (REG_POWER_CONTROL & 0x90) == 0x90) {
        // The card is currently powered - power it down
        pDevice->PowerOff();
        GPIO.ClearInterrupt(8); // Clear EINT8 (SYSINTR_PCMCIA_LEVEL)
    }

    MAPPEDIOPCMCIA.pDevice = &PCMCIADefaultDevice;
    MAPPEDMEMPCMCIA.pDevice = &PCMCIADefaultDevice;

    return true;
}

void IOCONTROLPCMCIA::InsertCard(PCMCIADevice* pCard)
{
    EnterCriticalSection(&IOLock);
    pDevice=pCard;
    MAPPEDIOPCMCIA.pDevice = pCard ? pCard : &PCMCIADefaultDevice;
    MAPPEDMEMPCMCIA.pDevice = pCard ? pCard : &PCMCIADefaultDevice;

    REG_CARD_STATUS_CHANGE |= 0x8;    // set Card Detect Change

    if (REG_STATUS_CHANGE_INT_CONFIG.Bits.CFG_CARD_DETECT_ENABLE) {
        // Raise SYSINTR_PCMCIA_STATE.  REG_STATUS_CHANGE_INT_CONFIG.Bits.ManagementIRQSelect
        // is sometimes 3 and other times 0.  In both cases, raise EINT3:  it appears that
        // the -INTR line on the PCMCIA controller is directly wired to EINT3.
        InterruptController.RaiseInterrupt(InterruptController.SourceEINT3);
    }

    Configuration.PCMCIACardInserted = (pCard) ? true : false;
    LeaveCriticalSection(&IOLock);
}

bool IOCONTROLPCMCIA::IsCardInserted(void)
{
    return (pDevice) ? true : false;
}

void IOCONTROLPCMCIA::RemoveCard(void)
{
    if (IsCardInserted() && (REG_POWER_CONTROL & 0x90) == 0x90) {
        // The card is currently powered - power it down
        pDevice->PowerOff();
        GPIO.ClearInterrupt(8); // Clear EINT8 (SYSINTR_PCMCIA_LEVEL)
    }

    InsertCard(NULL);
}

// This must be called with the IOLock held
void IOCONTROLPCMCIA::RaiseCardIRQ(void)
{
    if (REG_INTERRUPT_AND_GENERAL_CONTROL.Bits.CardIRQSelect) {
        // CardIRQSelect is actually ignored - PCMICA cards always raise EINT8,
        // and the PCMCIA driver "routes" the interrupt to the appropriate card
        // via its interrupt number.  The numbers are irrelevent... just a unique
        // identifier for each card.
        ASSERT(IsCardInserted()); // ensure the card isn't interrupting after it was removed!
        GPIO.RaiseInterrupt(8); // Raise EINT8 (SYSINTR_PCMCIA_LEVEL)
    }
}

// This must be called with the IOLock held
void IOCONTROLPCMCIA::ClearCardIRQ(void)
{
    if (REG_INTERRUPT_AND_GENERAL_CONTROL.Bits.CardIRQSelect) {
        // CardIRQSelect is actually ignored - PCMICA cards always raise EINT8,
        // and the PCMCIA driver "routes" the interrupt to the appropriate card
        // via its interrupt number.  The numbers are irrelevent... just a unique
        // identifier for each card.
        GPIO.ClearInterrupt(8); // Clear EINT8 (SYSINTR_PCMCIA_LEVEL)
    }
}


bool IOCONTROLPCMCIA::MapIOAddress(unsigned __int32* pIOAddress)
{
    if (!IsCardInserted() || ((REG_POWER_CONTROL & 0x90) != 0x90)) {
        // No card present, or it is not powered
        return false;
    }

    unsigned __int32 IOAddress = *pIOAddress + 0x3e0;

    if (REG_WINDOW_ENABLE.Bits.WIN_IO_MAP1_ENABLE && 
        IOAddress >= REG_IO_MAP1_START_ADDR.HalfWord && 
        IOAddress <= REG_IO_MAP1_END_ADDR.HalfWord) {

        // The address is within a PCMCIA card plugged into a slot
        *pIOAddress=IOAddress-REG_IO_MAP1_START_ADDR.HalfWord;
        return true;
    }
    
    else if (REG_WINDOW_ENABLE.Bits.WIN_IO_MAP0_ENABLE && 
        IOAddress >= REG_IO_MAP0_START_ADDR.HalfWord && 
        IOAddress <= REG_IO_MAP0_END_ADDR.HalfWord) {

        // The address is within a PCMCIA card plugged into a slot
        *pIOAddress=IOAddress-REG_IO_MAP0_START_ADDR.HalfWord;
        return true;

    } 
    return false;
}

#ifdef DEBUG // Currently this code doesn't work

bool __fastcall IOUSBHostController::PowerOn(void)
{
    HcRmInterval.Bits.FrameInterval=11999;
    return true;
}

unsigned __int32 __fastcall IOUSBHostController::ReadWord(unsigned __int32 IOAddress)
{
    switch (IOAddress) {
    case 0:
        return 0x10; // HcRevision
    case 4:
        return HcControl;
    case 8:
        return HcCommandStatus.Word;
    case 0xc:
        return HcInterruptStatus;
    case 0x10:
        return HcInterruptEnable;
    case 0x14:
        return HcInterruptDisable;
    case 0x18:
        return HcHCCA;
    case 0x1c:
        return HcPeriodCuttentED;
    case 0x20:
        return HcControlHeadED;
    case 0x24:
        return HcControlCurrentED;
    case 0x28:
        return HcBulkHeadED;
    case 0x2c:
        return HcBulkCurrentED;
    case 0x30:
        return HcDoneHead;
    case 0x34:
        return HcRmInterval.Word;
    case 0x38:
        return HcFmRemaining;
    case 0x3c:
        return HcFmNumber;
    case 0x40:
        return HcPeriodicStart;
    case 0x44:
        return HcLSThreshold;
    case 0x48:
        return HcRhDescriptorA;
    case 0x4c:
        return HcRhDescriptorB;
    case 0x50:
        return HcRhStatus;
    case 0x54:
        return HcRhPortStatus1;
    case 0x58:
        return HcRhPortStatus2;
    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        return 0;
    }
}

void __fastcall IOUSBHostController::WriteWord(unsigned __int32 IOAddress, unsigned __int32 Value)
{
    switch (IOAddress) {
    case 0:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE); // HcRevision is read-only
        break;
    case 4:
        HcControl=Value;
        break;
    case 8:
        HcCommandStatus.Word=Value;
        if (HcCommandStatus.Bits.HCR) { // Reset the controller
            HcControl=0;
            HcCommandStatus.Word=0;
        }
        break;
    case 0xc:
        HcInterruptStatus=Value;
        break;
    case 0x10:
        HcInterruptEnable=Value;
        break;
    case 0x14:
        HcInterruptDisable=Value;
        break;
    case 0x18:
        HcHCCA=Value;
        break;
    case 0x1c:
        HcPeriodCuttentED=Value;
        break;
    case 0x20:
        HcControlHeadED=Value;
        break;
    case 0x24:
        HcControlCurrentED=Value;
        break;
    case 0x28:
        HcBulkHeadED=Value;
        break;
    case 0x2c:
        HcBulkCurrentED=Value;
        break;
    case 0x30:
        HcDoneHead=Value;
        break;
    case 0x34:
        HcRmInterval.Word=Value;
        HcRmInterval.Bits.FrameInterval=11999;
        break;
    case 0x38:
        HcFmRemaining=Value;
        break;
    case 0x3c:
        HcFmNumber=Value;
        break;
    case 0x40:
        HcPeriodicStart=Value;
        break;
    case 0x44:
        HcLSThreshold=Value;
        break;
    case 0x48:
        HcRhDescriptorA=Value;
        break;
    case 0x4c:
        HcRhDescriptorB=Value;
        break;
    case 0x50:
        HcRhStatus=Value;
        break;
    case 0x54:
        HcRhPortStatus1=Value;
        break;
    case 0x58:
        HcRhPortStatus2=Value;
        break;
    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
    }
}

unsigned __int8 __fastcall IOUSBDevice::ReadByte(unsigned __int32 IOAddress)
{
    switch (IOAddress) {
    case 0:
        return udcFAR.Byte;
    case 4:
        return PMR.Byte;
    case 8:
        return EIR.Byte;
    case 0x18:
        return UIR.Byte;
    case 0x1c:
        return EIER.Byte;
    case 0x2c:
        return UIER.Byte;
    case 0x30:
        return FNR1;
    case 0x34:
        return FNR2;
    case 0x38:
        return INDEX;
    case 0x40:
        return MAXP.Byte;
    case 0x44:
        return EP0ICSR1.Byte;
    case 0x48:
        return ICSR2.Byte;
    case 0x50:
        return OCSR1.Byte;
    case 0x54:
        return OCSR2.Byte;
    case 0x58:
        return OFCR1;
    case 0x5c:
        return OFCR2;
    case 0x80:
        return EP0F;
    case 0x84:
        return EP1F;
    case 0x88:
        return EP2F;
    case 0x8c:
        return EP3F;
    case 0x90:
        return EP4F;
    case 0xc0:
        return EP1DC.Byte;
    case 0xc4:
        return EP1DU;
    case 0xc8:
        return EP1DF;
    case 0xcc:
        return EP1DTL;
    case 0xd0:
        return EP1DTM;
    case 0xd4:
        return EP1DTH;
    case 0xd8:
        return EP2DC.Byte;
    case 0xdc:
        return EP2DU;
    case 0xe0:
        return EP2DF;
    case 0xe4:
        return EP2DTL;
    case 0xe8:
        return EP2DTM;
    case 0xec:
        return EP2DTH;
    case 0x100:
        return EP3DC.Byte;
    case 0x104:
        return EP3DU;
    case 0x108:
        return EP3DF;
    case 0x10c:
        return EP3DTL;
    case 0x110:
        return EP3DTM;
    case 0x114:
        return EP3DTH;
    case 0x118:
        return EP4DC.Byte;
    case 0x11c:
        return EP4DU;
    case 0x120:
        return EP4DF;
    case 0x124:
        return EP4DTL;
    case 0x128:
        return EP4DTM;
    case 0x12c:
        return EP4DTH;
    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        return 0;
    }
}

void __fastcall IOUSBDevice::WriteByte(unsigned __int32 IOAddress, unsigned __int8 Value)
{
    switch (IOAddress) {
    case 0:
        udcFAR.Byte=Value;
        break;
    case 4:
        PMR.Byte=Value;
        break;
    case 8:
        EIR.Byte=Value;
        break;
    case 0x18:
        UIR.Byte=Value;
        break;
    case 0x1c:
        EIER.Byte=Value;
        break;
    case 0x2c:
        UIER.Byte=Value;
        break;
    case 0x30:
        FNR1=Value;
        break;
    case 0x34:
        FNR2=Value;
        break;
    case 0x38:
        INDEX=Value;
        break;
    case 0x40:
        MAXP.Byte=Value;
        break;
    case 0x44:
        EP0ICSR1.Byte=Value;
        break;
    case 0x48:
        ICSR2.Byte=Value;
        break;
    case 0x50:
        OCSR1.Byte=Value;
        break;
    case 0x54:
        OCSR2.Byte=Value;
        break;
    case 0x58:
        OFCR1=Value;
        break;
    case 0x5c:
        OFCR2=Value;
        break;
    case 0x80:
        EP0F=Value;
        break;
    case 0x84:
        EP1F=Value;
        break;
    case 0x88:
        EP2F=Value;
        break;
    case 0x8c:
        EP3F=Value;
        break;
    case 0x90:
        EP4F=Value;
        break;
    case 0xc0:
        EP1DC.Byte=Value;
        break;
    case 0xc4:
        EP1DU=Value;
        break;
    case 0xc8:
        EP1DF=Value;
        break;
    case 0xcc:
        EP1DTL=Value;
        break;
    case 0xd0:
        EP1DTM=Value;
        break;
    case 0xd4:
        EP1DTH=Value;
        break;
    case 0xd8:
        EP2DC.Byte=Value;
        break;
    case 0xdc:
        EP2DU=Value;
        break;
    case 0xe0:
        EP2DF=Value;
        break;
    case 0xe4:
        EP2DTL=Value;
        break;
    case 0xe8:
        EP2DTM=Value;
        break;
    case 0xec:
        EP2DTH=Value;
        break;
    case 0x100:
        EP3DC.Byte=Value;
        break;
    case 0x104:
        EP3DU=Value;
        break;
    case 0x108:
        EP3DF=Value;
        break;
    case 0x10c:
        EP3DTL=Value;
        break;
    case 0x110:
        EP3DTM=Value;
        break;
    case 0x114:
        EP3DTH=Value;
        break;
    case 0x118:
        EP4DC.Byte=Value;
        break;
    case 0x11c:
        EP4DU=Value;
        break;
    case 0x120:
        EP4DF=Value;
        break;
    case 0x124:
        EP4DTL=Value;
        break;
    case 0x128:
        EP4DTM=Value;
        break;
    case 0x12c:
        EP4DTH=Value;
        break;
    default:
        TerminateWithMessage(ID_MESSAGE_UNSUPPORTED_HARDWARE);
        break;
    }
}
#endif // DEBUG - currently this code doesn't work
