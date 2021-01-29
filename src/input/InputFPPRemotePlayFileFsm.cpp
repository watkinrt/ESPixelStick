/*
* InputFPPRemotePlayFileFsm.cpp
*
* Project: ESPixelStick - An ESP8266 / ESP32 and E1.31 based pixel driver
* Copyright (c) 2021 Shelby Merrick
* http://www.forkineye.com
*
*  This program is provided free for you to use in any way that you wish,
*  subject to the laws and regulations where you are using it.  Due diligence
*  is strongly suggested before using this code.  Please give credit where due.
*
*  The Author makes no warranty of any kind, express or implied, with regard
*  to this program or the documentation contained in this document.  The
*  Author shall not be liable in any event for incidental or consequential
*  damages in connection with, or arising out of, the furnishing, performance
*  or use of these programs.
*
*   Fsm object used to parse and play an File
*/

#include "InputFPPRemotePlayFile.hpp"
#include "../service/FPPDiscovery.h"
#include "../service/fseq.h"
#include "InputMgr.hpp"

//-----------------------------------------------------------------------------
void fsm_PlayFile_state_Idle::Poll (uint8_t * Buffer, size_t BufferSize)
{
    // DEBUG_START;

    // do nothing

    // DEBUG_END;

} // fsm_PlayFile_state_Idle::Poll

//-----------------------------------------------------------------------------
void fsm_PlayFile_state_Idle::Init (c_InputFPPRemotePlayFile* Parent)
{
    // DEBUG_START;

    // DEBUG_V (String (" Parent: '") + String ((uint32_t)Parent) + "'");
    p_InputFPPRemotePlayFile = Parent;
    p_InputFPPRemotePlayFile->pCurrentFsmState = &(Parent->fsm_PlayFile_state_Idle_imp);

    // DEBUG_END;

} // fsm_PlayFile_state_Idle::Init

//-----------------------------------------------------------------------------
void fsm_PlayFile_state_Idle::Start (String & FileName, uint32_t FrameId)
{
    // DEBUG_START;

    p_InputFPPRemotePlayFile->PlayItemName = FileName;
    p_InputFPPRemotePlayFile->LastFrameId = FrameId;

    // DEBUG_V (String ("    FileName: ") + p_InputFPPRemotePlayFile->PlayItemName);
    // DEBUG_V (String ("     FrameId: ") + p_InputFPPRemotePlayFile->LastFrameId);
    // DEBUG_V (String (" RepeatCount: ") + p_InputFPPRemotePlayFile->RepeatCount);

    p_InputFPPRemotePlayFile->fsm_PlayFile_state_PlayingFile_imp.Init (p_InputFPPRemotePlayFile);

    // DEBUG_END;

} // fsm_PlayFile_state_Idle::Start

//-----------------------------------------------------------------------------
void fsm_PlayFile_state_Idle::Stop (void)
{
    // DEBUG_START;

    // Do nothing

    // DEBUG_END;

} // fsm_PlayFile_state_Idle::Stop

//-----------------------------------------------------------------------------
bool fsm_PlayFile_state_Idle::Sync (uint32_t FrameId)
{
    // DEBUG_START;

    // Do Nothing

    // DEBUG_END;

    return false;

} // fsm_PlayFile_state_Idle::Sync

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void fsm_PlayFile_state_PlayingFile::Poll (uint8_t * Buffer, size_t BufferSize)
{
    // DEBUG_START;

    do // once
    {
        uint32_t frame = (millis () - p_InputFPPRemotePlayFile->StartTimeInMillis) / p_InputFPPRemotePlayFile->FrameStepTime;
        // have we reached the end of the file?
        if (p_InputFPPRemotePlayFile->TotalNumberOfFramesInSequence <= frame)
        {
            // DEBUG_V (String ("RepeatCount: ") + p_InputFPPRemotePlayFile->RepeatCount);
            if (0 != p_InputFPPRemotePlayFile->RepeatCount)
            {
                LOG_PORT.println (String("Start Playing:: FileName:  '") + p_InputFPPRemotePlayFile->GetFileName() + "'");
                --p_InputFPPRemotePlayFile->RepeatCount;
                // DEBUG_V (String ("RepeatCount: ") + p_InputFPPRemotePlayFile->RepeatCount);

                p_InputFPPRemotePlayFile->StartTimeInMillis = millis ();
                p_InputFPPRemotePlayFile->LastFrameId = -1;
                frame = 0;
            }
            else
            {
                // DEBUG_V (String ("Stop"));
                Stop ();
                break;
            }
        }

        if (frame == p_InputFPPRemotePlayFile->LastFrameId)
        {
            // DEBUG_V (String ("keep waiting"));
            break;
        }

        uint32_t pos = p_InputFPPRemotePlayFile->DataOffset + (p_InputFPPRemotePlayFile->ChannelsPerFrame * frame);
        int toRead = (p_InputFPPRemotePlayFile->ChannelsPerFrame > BufferSize) ? BufferSize : p_InputFPPRemotePlayFile->ChannelsPerFrame;

        //LOG_PORT.printf_P ( PSTR("%d / %d / %d / %d / %d\n"), dataOffset, channelsPerFrame, outputBufferSize, toRead, pos);
        size_t bytesRead = FileMgr.ReadSdFile (p_InputFPPRemotePlayFile->FileHandleForFileBeingPlayed, Buffer, toRead, pos);

        // DEBUG_V (String ("      pos: ") + String (pos));
        // DEBUG_V (String ("   toRead: ") + String (toRead));
        // DEBUG_V (String ("bytesRead: ") + String (bytesRead));

        if (bytesRead != toRead)
        {
            // DEBUG_V (String ("pos:                           ") + String (pos));
            // DEBUG_V (String ("toRead:                        ") + String (toRead));
            // DEBUG_V (String ("bytesRead:                     ") + String (bytesRead));
            // DEBUG_V (String ("TotalNumberOfFramesInSequence: ") + String (TotalNumberOfFramesInSequence));
            // DEBUG_V (String ("frame:                         ") + String (frame));

            LOG_PORT.println (F ("File Playback Failed to read enough data"));
            Stop ();
        }

        //LOG_PORT.printf_P( PSTR("New Frame!   Old: %d     New:  %d      Offset: %d\n)", fseqCurrentFrameId, frame, FileOffsetToCurrentHeaderRecord);
        p_InputFPPRemotePlayFile->LastFrameId = frame;

        InputMgr.ResetBlankTimer ();

    } while (false);

    // DEBUG_END;

} // fsm_PlayFile_state_PlayingFile::Poll

//-----------------------------------------------------------------------------
void fsm_PlayFile_state_PlayingFile::Init (c_InputFPPRemotePlayFile* Parent)
{
    // DEBUG_START;

    p_InputFPPRemotePlayFile = Parent;
    p_InputFPPRemotePlayFile->pCurrentFsmState = &(Parent->fsm_PlayFile_state_PlayingFile_imp);

    do // once
    {
        // DEBUG_V (String ("FileName: '") + p_InputFPPRemotePlayFile->PlayItemName + "'");
        // DEBUG_V (String (" FrameId: '") + p_InputFPPRemotePlayFile->LastFrameId + "'");

        if (false == FileMgr.OpenSdFile (p_InputFPPRemotePlayFile->PlayItemName,
            c_FileMgr::FileMode::FileRead,
            p_InputFPPRemotePlayFile->FileHandleForFileBeingPlayed))
        {
            LOG_PORT.println (String (F ("StartPlaying:: Could not open file: filename: '")) + p_InputFPPRemotePlayFile->PlayItemName + "'");
            p_InputFPPRemotePlayFile->pCurrentFsmState->Stop ();
            break;
        }
        // DEBUG_V ("");

        FSEQHeader fsqHeader;
        size_t BytesRead = FileMgr.ReadSdFile (p_InputFPPRemotePlayFile->FileHandleForFileBeingPlayed, (uint8_t*)&fsqHeader, sizeof (fsqHeader));

        if (BytesRead != sizeof (fsqHeader))
        {
            LOG_PORT.println (String (F ("StartPlaying:: Could not start. ")) + p_InputFPPRemotePlayFile->PlayItemName + F (" File is too short"));
            p_InputFPPRemotePlayFile->pCurrentFsmState->Stop ();
            break;
        }
        // DEBUG_V ("");

        if (fsqHeader.majorVersion != 2 || fsqHeader.compressionType != 0)
        {
            LOG_PORT.println (String (F ("StartPlaying:: Could not start. ")) + p_InputFPPRemotePlayFile->PlayItemName + F (" is not a v2 uncompressed sequence"));
            p_InputFPPRemotePlayFile->pCurrentFsmState->Stop ();
            break;
        }
        // DEBUG_V ("");

        p_InputFPPRemotePlayFile->DataOffset = fsqHeader.dataOffset;
        p_InputFPPRemotePlayFile->ChannelsPerFrame = fsqHeader.channelCount;
        p_InputFPPRemotePlayFile->FrameStepTime = max((uint8_t)1, fsqHeader.stepTime);
        p_InputFPPRemotePlayFile->TotalNumberOfFramesInSequence = fsqHeader.TotalNumberOfFramesInSequence;
        p_InputFPPRemotePlayFile->StartTimeInMillis = millis () - (p_InputFPPRemotePlayFile->FrameStepTime * p_InputFPPRemotePlayFile->LastFrameId);

        // DEBUG_V (String ("               LastFrameId: ") + String (p_InputFPPRemotePlayFile->LastFrameId));
        // DEBUG_V (String ("                   DataOffset: ") + String (p_InputFPPRemotePlayFile->DataOffset));
        // DEBUG_V (String ("             ChannelsPerFrame: ") + String (p_InputFPPRemotePlayFile->ChannelsPerFrame));
        // DEBUG_V (String ("                FrameStepTime: ") + String (p_InputFPPRemotePlayFile->FrameStepTime));
        // DEBUG_V (String ("TotalNumberOfFramesInSequence: ") + String (p_InputFPPRemotePlayFile->TotalNumberOfFramesInSequence));
        // DEBUG_V (String ("            StartTimeInMillis: ") + String (p_InputFPPRemotePlayFile->StartTimeInMillis));

        LOG_PORT.println (String (F ("Start Playing:: FileName:  '")) + p_InputFPPRemotePlayFile->PlayItemName + "'");

    } while (false);

    // DEBUG_END;

} // fsm_PlayFile_state_PlayingFile::Init

//-----------------------------------------------------------------------------
void fsm_PlayFile_state_PlayingFile::Start (String & FileName, uint32_t FrameId)
{
    // DEBUG_START;

    // DEBUG_END;

} // fsm_PlayFile_state_PlayingFile::Start

//-----------------------------------------------------------------------------
void fsm_PlayFile_state_PlayingFile::Stop (void)
{
    // DEBUG_START;

    FileMgr.CloseSdFile (p_InputFPPRemotePlayFile->FileHandleForFileBeingPlayed);
    p_InputFPPRemotePlayFile->FileHandleForFileBeingPlayed = 0;
    p_InputFPPRemotePlayFile->fsm_PlayFile_state_Idle_imp.Init(p_InputFPPRemotePlayFile);

    // DEBUG_END;

} // fsm_PlayFile_state_PlayingFile::Stop

//-----------------------------------------------------------------------------
bool fsm_PlayFile_state_PlayingFile::Sync (uint32_t TargetFrameId)
{
    // DEBUG_START;
    bool response = false;

    uint32_t CurrentFrame = (millis () - p_InputFPPRemotePlayFile->StartTimeInMillis) / p_InputFPPRemotePlayFile->FrameStepTime;
    uint32_t FrameDiff = CurrentFrame - TargetFrameId;

    if (CurrentFrame < TargetFrameId)
    {
        FrameDiff = TargetFrameId - CurrentFrame;
    }

    // DEBUG_V (String (" CurrentFrame: ") + String (CurrentFrame));
    // DEBUG_V (String ("TargetFrameId: ") + String (TargetFrameId));
    // DEBUG_V (String ("    FrameDiff: ") + String (FrameDiff));

    if (2 < FrameDiff)
    {
        // DEBUG_V ("Need to adjust the start time");
        // DEBUG_V (String ("     CurrentFrame: ") + String (CurrentFrame));
        // DEBUG_V (String ("    TargetFrameId: ") + String (TargetFrameId));
        // DEBUG_V (String ("        FrameDiff: ") + String (FrameDiff));
        // DEBUG_V (String ("StartTimeInMillis: ") + String (p_InputFPPRemotePlayFile->StartTimeInMillis));

        p_InputFPPRemotePlayFile->StartTimeInMillis =
            millis () - (TargetFrameId * p_InputFPPRemotePlayFile->FrameStepTime);
        response = true;
        // DEBUG_V (String ("StartTimeInMillis: ") + String (p_InputFPPRemotePlayFile->StartTimeInMillis));
    }

    // DEBUG_END;
    return response;

} // fsm_PlayFile_state_PlayingFile::Sync
