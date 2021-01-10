/*
* c_FPPDiscovery.cpp

* Copyright (c) 2020 Shelby Merrick
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
*/

#include "FPPDiscovery.h"
#include "../input/InputMgr.hpp"

#include <Int64String.h>
#include "../FileMgr.hpp"

extern const String VERSION;

#ifdef ARDUINO_ARCH_ESP32
#   define FPP_TYPE_ID          0xC3
#   define FPP_VARIANT_NAME     "ESPixelStick-ESP32"

#else
#   define FPP_TYPE_ID          0xC2
#   define FPP_VARIANT_NAME     "ESPixelStick-ESP8266"
#endif

#define FPP_DISCOVERY_PORT 32320

//-----------------------------------------------------------------------------
typedef union
{
    struct
    {
        uint8_t  header[4];  //FPPD
        uint8_t  packet_type;
        uint16_t data_len;
    } __attribute__ ((packed));
    uint8_t raw[301];
} FPPPacket;

typedef union
{
    struct
    {
        uint8_t  header[4];  //FPPD
        uint8_t  packet_type;
        uint16_t data_len;
        uint8_t  ping_version;
        uint8_t  ping_subtype;
        uint8_t  ping_hardware;
        uint16_t versionMajor;
        uint16_t versionMinor;
        uint8_t  operatingMode;
        uint8_t  ipAddress[4];
        char  hostName[65];
        char  version[41];
        char  hardwareType[41];
        char  ranges[121];
    } __attribute__ ((packed));
    uint8_t raw[301];
} FPPPingPacket;

typedef union
{
    struct
    {
        uint8_t  header[4];  //FPPD
        uint8_t  packet_type;
        uint16_t data_len;
        uint8_t sync_action;
        uint8_t sync_type;
        uint32_t frame_number;
        float  seconds_elapsed;
        char filename[250];
    } __attribute__ ((packed));
    uint8_t raw[301];
} FPPMultiSyncPacket;

struct FSEQVariableDataHeader
{
    uint16_t    length;
    char        type[2];
//  uint8_t     data[???];

} __attribute__ ((packed));

struct FSEQRangeEntry
{
    uint8_t Start[3];
    uint8_t Length[3];

} __attribute__ ((packed));

struct FSEQHeader
{
    uint8_t  header[4];    // FSEQ
    uint16_t dataOffset;
    uint8_t  minorVersion;
    uint8_t  majorVersion;
    uint16_t headerLen;
    uint32_t channelCount;
    uint32_t TotalNumberOfFramesInSequence;
    uint8_t  stepTime;
    uint8_t  flags;
    uint8_t  compressionType;
    uint8_t  numCompressedBlocks;
    uint8_t  numSparseRanges;
    uint8_t  flags2;
    uint64_t id;
} __attribute__ ((packed));

//-----------------------------------------------------------------------------
c_FPPDiscovery::c_FPPDiscovery ()
{
    // DEBUG_START;
    // DEBUG_END;
}

//-----------------------------------------------------------------------------
void c_FPPDiscovery::begin ()
{
    // DEBUG_START;

    do // once
    {
        StopPlaying ();

        inFileUpload = false;
        hasBeenInitialized = true;

        // delay (100);

        IPAddress address = IPAddress (239, 70, 80, 80);

        // Try to listen to the broadcast port
        if (!udp.listen (FPP_DISCOVERY_PORT))
        {
            LOG_PORT.println (String (F ("FPPDiscovery FAILED to subscribed to broadcast messages")));
            break;
        }
        LOG_PORT.println (String (F ("FPPDiscovery subscribed to broadcast")));

        if (!udp.listenMulticast (address, FPP_DISCOVERY_PORT))
        {
            LOG_PORT.println (String (F ("FPPDiscovery FAILED to subscribed to multicast messages")));
            break;
        }
        LOG_PORT.println (String (F ("FPPDiscovery subscribed to multicast: ")) + address.toString ());
        udp.onPacket (std::bind (&c_FPPDiscovery::ProcessReceivedUdpPacket, this, std::placeholders::_1));

        PlayFile (AutoPlayFileName);

    } while (false);

    sendPingPacket ();

    // DEBUG_END;
} // begin

//-----------------------------------------------------------------------------
void c_FPPDiscovery::Disable ()
{
    IsEnabled = false;
} // Disable

//-----------------------------------------------------------------------------
void c_FPPDiscovery::Enable ()
{
    IsEnabled = true;
} // Enable

//-----------------------------------------------------------------------------
void c_FPPDiscovery::ReadNextFrame (uint8_t * CurrentOutputBuffer, uint16_t CurrentOutputBufferSize)
{
    // DEBUG_START;

    outputBuffer = CurrentOutputBuffer;
    outputBufferSize = CurrentOutputBufferSize;

    if (isRemoteRunning)
    {
        uint32_t frame = (millis () - fseqStartMillis) / frameStepTime;

        // have we reached the end of the file?
        if (TotalNumberOfFramesInSequence <= frame)
        {
            StopPlaying ();
            StartPlaying (AutoPlayFileName, 0);
            frame = (millis () - fseqStartMillis) / frameStepTime;
        }

        if (frame != fseqCurrentFrameId)
        {
            uint32_t pos = dataOffset + (channelsPerFrame * frame);
            int toRead = (channelsPerFrame > outputBufferSize) ? outputBufferSize : channelsPerFrame;

            //LOG_PORT.printf_P ( PSTR("%d / %d / %d / %d / %d\n"), dataOffset, channelsPerFrame, outputBufferSize, toRead, pos);
            size_t bytesRead = FileMgr.ReadSdFile (fseqFile, outputBuffer, toRead, pos);

            // DEBUG_V (String ("pos:       ") + String (pos));
            // DEBUG_V (String ("toRead:    ") + String (toRead));
            // DEBUG_V (String ("bytesRead: ") + String (bytesRead));

            if (bytesRead != toRead)
            {
                // DEBUG_V (String ("pos:                           ") + String (pos));
                // DEBUG_V (String ("toRead:                        ") + String (toRead));
                // DEBUG_V (String ("bytesRead:                     ") + String (bytesRead));
                // DEBUG_V (String ("TotalNumberOfFramesInSequence: ") + String (TotalNumberOfFramesInSequence));
                // DEBUG_V (String ("frame:                         ") + String (frame));

                LOG_PORT.println (F ("File Playback Failed to read enough data"));
                StopPlaying ();
                StartPlaying (AutoPlayFileName, 0);
            }
            else
            {
                //LOG_PORT.printf_P( PSTR("New Frame!   Old: %d     New:  %d      Offset: %d\n)", fseqCurrentFrameId, frame, FileOffsetToCurrentHeaderRecord);
                fseqCurrentFrameId = frame;

                InputMgr.ResetBlankTimer ();
            }
        }
    }

    // DEBUG_END;
} // ReadNextFrame

uint64_t read64 (uint8_t* buf, int idx) {
    uint32_t r1 = (int)(buf[idx + 3]) << 24;
    r1 |= (int)(buf[idx + 2]) << 16;
    r1 |= (int)(buf[idx + 1]) << 8;
    r1 |= (int)(buf[idx]);

    uint32_t r2 = (int)(buf[idx + 7]) << 24;
    r2 |= (int)(buf[idx + 6]) << 16;
    r2 |= (int)(buf[idx + 5]) << 8;
    r2 |= (int)(buf[idx + 4]);
    uint64_t r = r2;
    r <<= 32;
    r |= r1;
    return r;
}
uint32_t read32 (uint8_t* buf, int idx) {
    uint32_t r = (int)(buf[idx + 3]) << 24;
    r |= (int)(buf[idx + 2]) << 16;
    r |= (int)(buf[idx + 1]) << 8;
    r |= (int)(buf[idx]);
    return r;
}
uint32_t read24 (uint8_t* pData)
{
    return ((uint32_t)(pData[0]) |
            (uint32_t)(pData[1]) << 8 |
            (uint32_t)(pData[2]) << 16);
} // read24
uint16_t read16 (uint8_t* pData)
{
    return ((uint16_t)(pData[0]) |
            (uint16_t)(pData[1]) << 8);
} // read16

//-----------------------------------------------------------------------------
void c_FPPDiscovery::ProcessReceivedUdpPacket (AsyncUDPPacket _packet)
{
    // DEBUG_START;

    FPPPacket* packet = reinterpret_cast<FPPPacket*>(_packet.data ());
    // DEBUG_V ("Received FPP packet");
    // DEBUG_V (String("packet->packet_type: ") + String(packet->packet_type));

    switch (packet->packet_type)
    {
        case 0x04: //Ping Packet
        {
            FPPPingPacket* pingPacket = reinterpret_cast<FPPPingPacket*>(_packet.data ());

            if (pingPacket->ping_subtype == 0x01)
            {
                // DEBUG_V (String (F ("FPPPing discovery packet")));
                // received a discover ping packet, need to send a ping out
                if(_packet.isBroadcast() || _packet.isMulticast())
                    sendPingPacket();
                else
                    sendPingPacket(_packet.remoteIP());
            }
            break;
        }

        case 0x01: //Multisync packet
        {
            FPPMultiSyncPacket* msPacket = reinterpret_cast<FPPMultiSyncPacket*>(_packet.data ());
            // DEBUG_V (String (F ("msPacket->sync_type: ")) + String(msPacket->sync_type));

            if (msPacket->sync_type == 0x00)
            {
                //FSEQ type, not media
                // DEBUG_V (String (F ("Received FPP FSEQ sync packet")));
                ProcessSyncPacket (msPacket->sync_action, msPacket->filename, msPacket->frame_number);
            }
            break;
        }

        case 0x03: //Blank packet
        {
            // DEBUG_V (String (F ("FPP Blank packet")));
            ProcessBlankPacket ();
            break;
        }

        default:
        {
            // DEBUG_V (String ("UnHandled PDU: packet_type:  ") + String (packet->packet_type));
            break;
        }
    }

    // DEBUG_END;
}

//-----------------------------------------------------------------------------
void c_FPPDiscovery::ProcessSyncPacket (uint8_t action, String filename, uint32_t frame)
{
    // DEBUG_START;
    do // once
    {
        if (false == IsEnabled)
        {
            break;
        }

        if (!AllowedToRemotePlayFiles ())
        {
            break;
        }

        // DEBUG_V (String("action: ") + String(action));

        switch (action)
        {
            case 0x00: // Start
            {
                // DEBUG_V ("Start");
                if (filename != fseqName)
                {
                    ProcessSyncPacket (0x01, filename, frame); // stop
                    ProcessSyncPacket (0x03, filename, frame); // need to open the file
                }

                break;
            }

            case 0x01: // Stop
            {
                // DEBUG_V ("Stop");
                if (fseqName != "")
                {
                    FileMgr.CloseSdFile (fseqFile);
                }
                StopPlaying ();
                break;
            }

            case 0x02: // Sync
            {
                // DEBUG_V ("Sync");

                if (!isRemoteRunning || filename != fseqName)
                {
                    ProcessSyncPacket (0x00, filename, frame); // need to start first
                }

                if (isRemoteRunning)
                {
                    int diff = (frame - fseqCurrentFrameId);
                    if (diff > 2 || diff < -2)
                    {
                        // reset the start time which will then trigger a new frame time
                        fseqStartMillis = millis () - (frameStepTime * frame);
                        // DEBUF_V("Large diff %d\n", diff);
                    }
                }
                break;
            }

            case 0x03: // Open
            {
                StartPlaying (filename, frame);
                break;
            }

            default:
            {
                // DEBUG_V (String (F ("ERROR: Unknown Action: ")) + String (action));
                break;
            }

        } // switch
    } while (false);

    // DEBUG_END;
} // ProcessSyncPacket

//-----------------------------------------------------------------------------
void c_FPPDiscovery::ProcessBlankPacket ()
{
    // DEBUG_START;
    if (AllowedToRemotePlayFiles())
    {
        memset (outputBuffer, 0x0, outputBufferSize);
    }
    // DEBUG_END;
} // ProcessBlankPacket

//-----------------------------------------------------------------------------
void c_FPPDiscovery::sendPingPacket (IPAddress destination)
{
    // DEBUG_START;
    FPPPingPacket packet;
    memset (packet.raw, 0, sizeof (packet));
    packet.raw[0] = 'F';
    packet.raw[1] = 'P';
    packet.raw[2] = 'P';
    packet.raw[3] = 'D';
    packet.packet_type = 0x04;
    packet.data_len = 294;
    packet.ping_version = 0x3;
    packet.ping_subtype = 0x0; // 1 is to "discover" others, we don't need that
    packet.ping_hardware = FPP_TYPE_ID;

    const char* version = VERSION.c_str ();
    uint16_t v = (uint16_t)atoi (version);
    packet.versionMajor = (v >> 8) + ((v & 0xFF) << 8);
    v = (uint16_t)atoi (&version[2]);
    packet.versionMinor = (v >> 8) + ((v & 0xFF) << 8);

    packet.operatingMode = (AllowedToRemotePlayFiles()) ? 0x08 : 0x01; // Support remote mode : Bridge Mode

    uint32_t ip = static_cast<uint32_t>(WiFiMgr.getIpAddress ());
    memcpy (packet.ipAddress, &ip, 4);
    strcpy (packet.hostName, config.hostname.c_str());
    strcpy (packet.version, (VERSION + String (":") + BUILD_DATE).c_str());
    strcpy (packet.hardwareType, FPP_VARIANT_NAME);
    packet.ranges[0] = 0;

    // DEBUG_V ("Ping reply to " + destination.toString());
    udp.writeTo (packet.raw, sizeof (packet), destination, FPP_DISCOVERY_PORT);

    // DEBUG_END;
} // sendPingPacket

#ifdef PRINT_DEBUG
//-----------------------------------------------------------------------------
static void printReq (AsyncWebServerRequest* request, bool post)
{
    int params = request->params ();
    for (int i = 0; i < params; i++)
    {
        AsyncWebParameter* p = request->getParam (i);
        if (p->isFile ())
        { //p->isPost() is also true
            LOG_PORT.printf_P( PSTR("FILE[%s]: %s, size: %u\n"), p->name ().c_str (), p->value ().c_str (), p->size ());
        }
        else if (p->isPost ())
        {
            LOG_PORT.printf_P( PSTR("POST[%s]: %s\n"), p->name ().c_str (), p->value ().c_str ());
        }
        else
        {
            LOG_PORT.printf_P ( PSTR("GET[%s]: %s\n"), p->name ().c_str (), p->value ().c_str ());
        }
    }
} // printReq
#else
#define printReq
#endif // !def PRINT_DEBUG

//-----------------------------------------------------------------------------
void c_FPPDiscovery::BuildFseqResponse (String fname, c_FileMgr::FileId fseq, String & resp)
{
    // DEBUG_START;

    DynamicJsonDocument JsonDoc (4*1024);
    JsonObject JsonData = JsonDoc.to<JsonObject> ();

    FSEQHeader fsqHeader;
    FileMgr.ReadSdFile (fseq, (byte*)&fsqHeader, sizeof (fsqHeader), 0);

    JsonData[F("Name")]            = fname;
    JsonData[F("Version")]         = String (fsqHeader.majorVersion) + "." + String (fsqHeader.minorVersion);
    JsonData[F("ID")]              = int64String (fsqHeader.id);
    JsonData[F("StepTime")]        = String (fsqHeader.stepTime);
    JsonData[F("NumFrames")]       = String (fsqHeader.TotalNumberOfFramesInSequence);
    JsonData[F("CompressionType")] = fsqHeader.compressionType;

    uint32_t maxChannel = fsqHeader.channelCount;

    if (0 != fsqHeader.numSparseRanges)
    {
        JsonArray  JsonDataRanges = JsonData.createNestedArray (F("Ranges"));

        maxChannel = 0;

        uint8_t* RangeDataBuffer = (uint8_t*)malloc (6 * fsqHeader.numSparseRanges);
        FSEQRangeEntry* CurrentFSEQRangeEntry = (FSEQRangeEntry*)RangeDataBuffer;

        FileMgr.ReadSdFile (fseq, RangeDataBuffer, sizeof (FSEQRangeEntry), fsqHeader.numCompressedBlocks * 8 + 32);

        for (int CurrentRangeIndex = 0;
             CurrentRangeIndex < fsqHeader.numSparseRanges;
             CurrentRangeIndex++, CurrentFSEQRangeEntry++)
        {
            uint32_t RangeStart  = read24 (CurrentFSEQRangeEntry->Start);
            uint32_t RangeLength = read24 (CurrentFSEQRangeEntry->Length);

            JsonObject JsonRange = JsonDataRanges.createNestedObject ();
            JsonRange[F("Start")]  = String (RangeStart);
            JsonRange[F("Length")] = String (RangeLength);

            if ((RangeStart + RangeLength - 1) > maxChannel)
            {
                maxChannel = RangeStart + RangeLength - 1;
            }
        }

        free (RangeDataBuffer);
    }

    JsonData[F("MaxChannel")]   = String (maxChannel);
    JsonData[F("ChannelCount")] = String (fsqHeader.channelCount);

    uint32_t FileOffsetToCurrentHeaderRecord = read16 ((uint8_t*)&fsqHeader.headerLen);
    uint32_t FileOffsetToStartOfSequenceData = read16 ((uint8_t*)&fsqHeader.dataOffset); // DataOffset

    // DEBUG_V (String ("FileOffsetToCurrentHeaderRecord: ") + String (FileOffsetToCurrentHeaderRecord));
    // DEBUG_V (String ("FileOffsetToStartOfSequenceData: ") + String (FileOffsetToStartOfSequenceData));

    if (FileOffsetToCurrentHeaderRecord < FileOffsetToStartOfSequenceData)
    {
        JsonArray  JsonDataHeaders = JsonData.createNestedArray (F("variableHeaders"));

        char FSEQVariableDataHeaderBuffer[sizeof (FSEQVariableDataHeader) + 1];
        memset ((uint8_t*)FSEQVariableDataHeaderBuffer, 0x00, sizeof (FSEQVariableDataHeaderBuffer));
        FSEQVariableDataHeader* pCurrentVariableHeader = (FSEQVariableDataHeader*)FSEQVariableDataHeaderBuffer;

        while (FileOffsetToCurrentHeaderRecord < FileOffsetToStartOfSequenceData)
        {
            FileMgr.ReadSdFile (fseq, (byte*)FSEQVariableDataHeaderBuffer, sizeof (FSEQVariableDataHeader), FileOffsetToCurrentHeaderRecord);

            int VariableDataHeaderTotalLength = read16 ((uint8_t*)&(pCurrentVariableHeader->length));
            int VariableDataHeaderDataLength  = VariableDataHeaderTotalLength - sizeof (FSEQVariableDataHeader);

            String HeaderTypeCode (pCurrentVariableHeader->type);

            if ((HeaderTypeCode == "mf") || (HeaderTypeCode == "sp") )
            {
                char * VariableDataHeaderDataBuffer = (char*)malloc (VariableDataHeaderDataLength + 1);
                memset (VariableDataHeaderDataBuffer, 0x00, VariableDataHeaderDataLength + 1);

                FileMgr.ReadSdFile (fseq, (byte*)VariableDataHeaderDataBuffer, VariableDataHeaderDataLength, FileOffsetToCurrentHeaderRecord);

                JsonObject JsonDataHeader = JsonDataHeaders.createNestedObject ();
                JsonDataHeader[HeaderTypeCode] = String (VariableDataHeaderDataBuffer);

                free (VariableDataHeaderDataBuffer);
            }

            FileOffsetToCurrentHeaderRecord += VariableDataHeaderTotalLength + sizeof (FSEQVariableDataHeader);
        } // while there are headers to process
    } // there are headers to process

    serializeJson (JsonData, resp);

    // DEBUG_END;

} // BuildFseqResponse

//-----------------------------------------------------------------------------
void c_FPPDiscovery::ProcessGET (AsyncWebServerRequest* request)
{
    // DEBUG_START;
    printReq(request, false);

    do // once
    {
        if (!request->hasParam (F("path")))
        {
            request->send (404);
            break;
        }

        String path = request->getParam (F("path"))->value ();
        if (path.startsWith (F("/api/sequence/")) && AllowedToRemotePlayFiles())
        {
            String seq = path.substring (14);
            if (seq.endsWith (F("/meta")))
            {
                seq = seq.substring (0, seq.length () - 5);
                ProcessSyncPacket (0x1, "", 0); //must stop

                c_FileMgr::FileId FileHandle;
                if (FileMgr.OpenSdFile (seq, c_FileMgr::FileMode::FileRead, FileHandle))
                {
                    if (FileMgr.GetSdFileSize(FileHandle) > 0)
                    {
                        // found the file. return metadata as json
                        String resp = "";
                        BuildFseqResponse (seq, FileHandle, resp);
                        FileMgr.CloseSdFile (FileHandle);
                        request->send (200, F ("application/json"), resp);
                        break;
                    }
                }
                LOG_PORT.println (String (F ("FPP Discovery: Could not open: ")) + seq);
            }
        }
        request->send (404);

    } while (false); // do once

    // DEBUG_END;

} // ProcessGET

//-----------------------------------------------------------------------------
void c_FPPDiscovery::ProcessPOST (AsyncWebServerRequest* request)
{
    // DEBUG_START;
    printReq(request, true);

    do // once
    {
        String path = request->getParam (F("path"))->value ();
        // DEBUG_V (String(F("path: ")) + path);

        if (path != F("uploadFile"))
        {
            request->send (404);
            break;
        }

        String filename = request->getParam (F("filename"))->value ();
        // DEBUG_V (String(F("filename: ")) + filename);

        c_FileMgr::FileId FileHandle;
        if (false == FileMgr.OpenSdFile (filename, c_FileMgr::FileMode::FileRead, FileHandle))
        {
            LOG_PORT.println (String (F ("c_FPPDiscovery::ProcessPOST: File Does Not Exist - filename: ")) + filename);
            request->send (404);
            break;
        }

        String resp = "";
        BuildFseqResponse (filename, FileHandle, resp);
        FileMgr.CloseSdFile (FileHandle);
        request->send (200, F ("application/json"), resp);

    } while (false);

    // DEBUG_END;
}

//-----------------------------------------------------------------------------
void c_FPPDiscovery::ProcessFile (AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final)
{
    // DEBUG_START;
    //LOG_PORT.printf_P( PSTR("In ProcessFile: %s    idx: %d    RangeLength: %d    final: %d\n)",filename.c_str(), index, RangeLength, final? 1 : 0);

    //printReq(request, false);
    request->send (404);
    // DEBUG_END;

} // ProcessFile

//-----------------------------------------------------------------------------
// the blocks come in very small (~500 bytes) we'll accumulate in a buffer
// so the writes out to SD can be more in line with what the SD file system can handle
#define BUFFER_LEN 8192
void c_FPPDiscovery::ProcessBody (AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total)
{
    // DEBUG_START;

    if (!index)
    {
        //LOG_PORT.printf_P( PSTR("In process Body:    idx: %d    RangeLength: %d    total: %d\n)", index, RangeLength, total);
        printReq (request, false);

        String path = request->getParam (F("path"))->value ();
        if (path == F("uploadFile"))
        {
            ProcessSyncPacket (0x1, "", 0); //must stop

            inFileUpload = true;

            UploadFileName = request->getParam (F("filename"))->value ();
        }
    }

    if (inFileUpload)
    {
        FileMgr.handleFileUpload (UploadFileName, index, data, len, total <= (index + len));

        if (index + len == total)
        {
            inFileUpload = false;
        }
    }

    // DEBUG_END;
}

//-----------------------------------------------------------------------------
void c_FPPDiscovery::GetSysInfoJSON (JsonObject & jsonResponse)
{
    // DEBUG_START;
    DEBUG_V(config.hostname);
    jsonResponse[F ("HostName")]        = config.hostname;
    jsonResponse[F ("HostDescription")] = config.id;
    jsonResponse[F ("Platform")]        = F("ESPixelStick");
    jsonResponse[F ("Variant")]         = FPP_VARIANT_NAME;
    jsonResponse[F ("Mode")]            = (true == AllowedToRemotePlayFiles()) ? "remote" : "bridge";
    jsonResponse[F ("Version")]         = VERSION + String (":") + BUILD_DATE;

    const char* version = VERSION.c_str ();
    uint16_t v = (uint16_t)atoi (version);

    jsonResponse[F ("majorVersion")] = (uint16_t)atoi (version);
    jsonResponse[F ("minorVersion")] = (uint16_t)atoi (&version[2]);
    jsonResponse[F ("typeId")]       = FPP_TYPE_ID;

    JsonObject jsonResponseUtilization = jsonResponse.createNestedObject (F("Utilization"));
    jsonResponseUtilization[F("MemoryFree")] = ESP.getFreeHeap ();
    jsonResponseUtilization[F("Uptime")]     = millis ();

    jsonResponse[F ("rssi")] = WiFi.RSSI ();
    JsonArray jsonResponseIpAddresses = jsonResponse.createNestedArray (F ("IPS"));
    jsonResponseIpAddresses.add(WiFiMgr.getIpAddress().toString ());

    // DEBUG_END;

} // GetSysInfoJSON

//-----------------------------------------------------------------------------
void c_FPPDiscovery::ProcessFPPJson (AsyncWebServerRequest* request)
{
    // DEBUG_START;
    printReq(request, false);

    do // once
    {
        if (!request->hasParam ("command"))
        {
            request->send (404);
            // DEBUG_V (String ("Missing Param: 'command' "));

            break;
        }

        DynamicJsonDocument JsonDoc (2048);
        JsonObject JsonData = JsonDoc.to<JsonObject> ();

        String command = request->getParam ("command")->value ();
        // DEBUG_V (String ("command: ") + command);

        if (command == "getFPPstatus")
        {
            String adv = "false";
            if (request->hasParam ("advancedView"))
            {
                adv = request->getParam ("advancedView")->value ();
            }

            JsonObject JsonDataMqtt = JsonData.createNestedObject(F("MQTT"));

            JsonDataMqtt[F ("configured")] = false;
            JsonDataMqtt[F ("connected")]  = false;

            JsonObject JsonDataCurrentPlaylist = JsonData.createNestedObject (F ("current_playlist"));

            JsonDataCurrentPlaylist[F ("count")]       = "0";
            JsonDataCurrentPlaylist[F ("description")] = "";
            JsonDataCurrentPlaylist[F ("index")]       = "0";
            JsonDataCurrentPlaylist[F ("playlist")]    = "";
            JsonDataCurrentPlaylist[F ("type")]        = "";

            JsonData[F("volume")]         = 70;
            JsonData[F("media_filename")] = "";
            JsonData[F("fppd")]           = F("running");
            JsonData[F("current_song")]   = "";

            int mseconds      = fseqCurrentFrameId * frameStepTime;
            int msecondsTotal = frameStepTime * TotalNumberOfFramesInSequence;

            int secs    = mseconds / 1000;
            int secsTot = msecondsTotal / 1000;

            JsonData[F ("current_sequence")] = fseqName;
            JsonData[F ("playlist")] = fseqName;
            JsonData[F ("seconds_elapsed")] = String (secs);
            JsonData[F ("seconds_played")] = String (secs);
            JsonData[F ("seconds_remaining")] = String (secsTot - secs);
            JsonData[F ("sequence_filename")] = fseqName;

            if (false == isRemoteRunning)
            {
                JsonData[F ("status")] = 0;
                JsonData[F ("status_name")] = F ("idle");
            }
            else
            {
                JsonData[F ("status")] = 1;
                JsonData[F ("status_name")] = F ("playing");
            }

            int mins = secs / 60;
            secs = secs % 60;

            secsTot    = secsTot - secs;
            int minRem = secsTot / 60;
            secsTot    = secsTot % 60;

            char buf[8];
            sprintf (buf, "%02d:%02d", mins, secs);
            JsonData[F ("time_elapsed")] = buf;

            sprintf (buf, "%02d:%02d", minRem, secsTot);
            JsonData[F ("time_remaining")] = buf;

            if (AllowedToRemotePlayFiles())
            {
                JsonData[F("mode")] = 8;
                JsonData[F("mode_name")] = F ("remote");
            }
            else
            {
                JsonData[F("mode")] = 1;
                JsonData[F("mode_name")] = F("bridge");
            }

            if (adv == "true")
            {
                JsonObject JsonDataAdvancedView = JsonData.createNestedObject (F ("advancedView"));
                GetSysInfoJSON (JsonDataAdvancedView);
            }

            String Response;
            serializeJson (JsonDoc, Response);
            // DEBUG_V (String ("JsonDoc: ") + Response);
            request->send (200, F("application/json"), Response);

            break;
        }

        if (command == F("getSysInfo"))
        {
            GetSysInfoJSON (JsonData);

            String resp = "";
            serializeJson (JsonData, resp);
            // DEBUG_V (String ("JsonDoc: ") + resp);
            request->send (200, F("application/json"), resp);

            break;
        }

        if (command == F("getHostNameInfo"))
        {
            JsonData[F("HostName")] = config.hostname;
            JsonData[F("HostDescription")] = config.id;

            String resp;
            serializeJson (JsonData, resp);
            // DEBUG_V (String ("resp: ") + resp);
            request->send (200, F("application/json"), resp);

            break;
        }

        // DEBUG_V (String ("Unknown command: ") + command);
        request->send (404);

    } while (false);

    // DEBUG_END;

} // ProcessFPPJson

//-----------------------------------------------------------------------------
void c_FPPDiscovery::StartPlaying (String & filename, uint32_t frameId)
{
    // DEBUG_START;
    // DEBUG_V (String("Open:: filename: ") + filename);

    do // once
    {
        if (isRemoteRunning || filename != "")
        {
            ProcessSyncPacket (0x01, filename, frameId); //need to stop first
        }

        // clear the play file tracking data
        StopPlaying ();

        if (inFileUpload || failedFseqName == filename)
        {
            // DEBUG_V ("Uploading or this file failed to previously open");
            break;
        }

        if ((String("...") == filename) || (0 == filename.length()))
        {
            // DEBUG_V("ignore the not playing a file indicator");
            break;
        }

         if(false == FileMgr.OpenSdFile (filename, c_FileMgr::FileMode::FileRead, fseqFile))
        {
            LOG_PORT.println (String (F("FPPDiscovery::StartPlaying:: Could not open file: filename: ")) + filename);
            failedFseqName = filename;
            FileMgr.CloseSdFile (fseqFile);
            break;
        }

        FSEQHeader fsqHeader;
        size_t BytesRead = FileMgr.ReadSdFile (fseqFile , (uint8_t*)&fsqHeader, sizeof (fsqHeader));

        if (BytesRead != sizeof (fsqHeader))
        {
            LOG_PORT.println (String (F ("FPPDiscovery::StartPlaying:: Could not start. ")) + filename + F (" File is too short"));
            break;
        }

        if (fsqHeader.majorVersion != 2 || fsqHeader.compressionType != 0)
        {
            LOG_PORT.println (String (F("FPPDiscovery::StartPlaying:: Could not start. ")) + filename + F(" is not a v2 uncompressed sequence"));
            // DEBUG_V ("not a v2 uncompressed sequence");

            failedFseqName = filename;
            FileMgr.CloseSdFile (fseqFile);

            break;
        }

        // DEBUG_V ("Starting file output");

        isRemoteRunning               = true;
        fseqName                      = filename;
        fseqCurrentFrameId            = 0;
        dataOffset                    = fsqHeader.dataOffset;
        channelsPerFrame              = fsqHeader.channelCount;
        frameStepTime                 = fsqHeader.stepTime;
        TotalNumberOfFramesInSequence = fsqHeader.TotalNumberOfFramesInSequence;
        fseqStartMillis               = millis () - (frameStepTime * frameId);

        // DEBUG_V (String ("TotalNumberOfFramesInSequence: ") + String (TotalNumberOfFramesInSequence));
        // DEBUG_V (String ("frameStepTime:                 ") + String (frameStepTime));
        // DEBUG_V (String ("channelsPerFrame:              ") + String (channelsPerFrame));
        // DEBUG_V (String ("dataOffset:                    ") + String (dataOffset));

        LOG_PORT.println (String (F ("FPPDiscovery::StartPlaying:: Playing:  '")) + filename + "'" );

    } while (false);

    // DEBUG_END;

} // StartPlaying

//-----------------------------------------------------------------------------
void c_FPPDiscovery::StopPlaying ()
{
    // DEBUG_START;

    if (0 != fseqName.length ())
    {
        LOG_PORT.println (String (F ("FPPDiscovery::StopPlaying '")) + fseqName + "'");
    }

    isRemoteRunning = false;

    fseqName = "";
    fseqCurrentFrameId = 0;
    frameStepTime = 0;
    TotalNumberOfFramesInSequence = 0;
    dataOffset = 0;
    channelsPerFrame = 0;

    FileMgr.CloseSdFile (fseqFile);

    // blank the display
    memset (outputBuffer, 0x0, outputBufferSize);

    // DEBUG_END;

} // StopPlaying

//-----------------------------------------------------------------------------
void c_FPPDiscovery::PlayFile (String & NewFileName)
{
    // DEBUG_START;
    // Having an autoplay file means the AutoPlay file takes precedence over the remote player
    // if no file is playing then just start the new autoplay file.
    // if the AP file is empty then revert to remote operation.
    // if already in remote, do nothing

    // are we playing a file
    if ((0 != fseqName.length ()) && (AutoPlayFileName != NewFileName))
    {
        // Whatever we are playing, it is not the new autoplay file
        StopPlaying ();
    }

    AutoPlayFileName = NewFileName;

    // DEBUG_V ();

    // do we have an autoplay file to play?
    if ((0 != AutoPlayFileName.length ()) && (true == hasBeenInitialized))
    {
        // start playing the new autoplay file
        StartPlaying (AutoPlayFileName, 0);
    }

    // DEBUG_END;

} // PlayFile

//-----------------------------------------------------------------------------
bool c_FPPDiscovery::AllowedToRemotePlayFiles()
{
    return ((FileMgr.SdCardIsInstalled() == true) && (String(F(Stop_FPP_RemotePlay)) == AutoPlayFileName));
//    return (hasSDStorage == true);
} // AllowedToRemotePlayFiles


c_FPPDiscovery FPPDiscovery;
