/*
* WS2811Uart.cpp - WS2811 driver code for ESPixelStick UART
*
* Project: ESPixelStick - An ESP8266 / ESP32 and E1.31 based pixel driver
* Copyright (c) 2015 Shelby Merrick
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

#include "../ESPixelStick.h"
#include "OutputWS2811.hpp"

//----------------------------------------------------------------------------
c_OutputWS2811::c_OutputWS2811 (c_OutputMgr::e_OutputChannelIds OutputChannelId,
    gpio_num_t outputGpio,
    uart_port_t uart,
    c_OutputMgr::e_OutputType outputType) :
    c_OutputCommon (OutputChannelId, outputGpio, uart, outputType),
    color_order ("rgb"),
    pixel_count (100),
    zig_size (0),
    group_size (1),
    gamma (1.0),
    brightness (100),
    pNextIntensityToSend (nullptr),
    RemainingPixelCount (0),
    numIntensityBytesPerPixel (3),
    InterFrameGapInMicroSec (WS2811_MIN_IDLE_TIME_US)
{
    // DEBUG_START;
    ColorOffsets.offset.r = 0;
    ColorOffsets.offset.g = 1;
    ColorOffsets.offset.b = 2;
    ColorOffsets.offset.w = 3;

    // DEBUG_END;
} // c_OutputWS2811

//----------------------------------------------------------------------------
c_OutputWS2811::~c_OutputWS2811 ()
{
    // DEBUG_START;

    // DEBUG_END;
} // ~c_OutputWS2811

//----------------------------------------------------------------------------
void c_OutputWS2811::GetConfig (ArduinoJson::JsonObject& jsonConfig)
{
    // DEBUG_START;

    jsonConfig[CN_color_order] = color_order;
    jsonConfig[CN_pixel_count] = pixel_count;
    jsonConfig[CN_group_size] = group_size;
    jsonConfig[CN_zig_size] = zig_size;
    jsonConfig[CN_gamma] = gamma;
    jsonConfig[CN_brightness] = brightness; // save as a 0 - 100 percentage
    jsonConfig[CN_interframetime] = InterFrameGapInMicroSec;

    c_OutputCommon::GetConfig (jsonConfig);

    // DEBUG_END;
} // GetConfig

//----------------------------------------------------------------------------
void c_OutputWS2811::GetStatus (ArduinoJson::JsonObject& jsonStatus)
{
    c_OutputCommon::GetStatus (jsonStatus);
    // uint32_t UartIntSt = GET_PERI_REG_MASK (UART_INT_ST (UartId), UART_TXFIFO_EMPTY_INT_ENA);
    // uint16_t SpaceInFifo = (((uint16_t)UART_TX_FIFO_SIZE) - (getWS2811FifoLength));

    // jsonStatus["pNextIntensityToSend"] = uint32_t(pNextIntensityToSend);
    // jsonStatus["RemainingIntensityCount"] = uint32_t(RemainingIntensityCount);
    // jsonStatus["UartIntSt"] = UartIntSt;
    // jsonStatus["SpaceInFifo"] = SpaceInFifo;

} // GetStatus

//----------------------------------------------------------------------------
void c_OutputWS2811::SetOutputBufferSize (uint16_t NumChannelsAvailable)
{
    // DEBUG_START;
    // DEBUG_V (String ("NumChannelsAvailable: ") + String (NumChannelsAvailable));
    // DEBUG_V (String ("   GetBufferUsedSize: ") + String (c_OutputCommon::GetBufferUsedSize ()));
    // DEBUG_V (String ("         pixel_count: ") + String (pixel_count));
    // DEBUG_V (String ("       BufferAddress: ") + String ((uint32_t)(c_OutputCommon::GetBufferAddress ())));

    do // once
    {
        // are we changing size?
        if (NumChannelsAvailable == OutputBufferSize)
        {
            // DEBUG_V ("NO Need to change the ISR buffer");
            break;
        }

        // DEBUG_V ("Need to change the ISR buffer");

        // Stop current output operation
        c_OutputCommon::SetOutputBufferSize (NumChannelsAvailable);

        // Calculate our refresh time
        FrameMinDurationInMicroSec = (WS2811_MICRO_SEC_PER_INTENSITY * OutputBufferSize) + InterFrameGapInMicroSec;

        } while (false);

        // DEBUG_END;
} // SetBufferSize

//----------------------------------------------------------------------------
/* Process the config
*
*   needs
*       reference to string to process
*   returns
*       true - config has been accepted
*       false - Config rejected. Using defaults for invalid settings
*/
bool c_OutputWS2811::SetConfig (ArduinoJson::JsonObject& jsonConfig)
{
    // DEBUG_START;

    // enums need to be converted to uints for json
    setFromJSON (color_order, jsonConfig, CN_color_order);
    setFromJSON (pixel_count, jsonConfig, CN_pixel_count);
    setFromJSON (group_size, jsonConfig, CN_group_size);
    setFromJSON (zig_size, jsonConfig, CN_zig_size);
    setFromJSON (gamma, jsonConfig, CN_gamma);
    setFromJSON (brightness, jsonConfig, CN_brightness);
    setFromJSON (InterFrameGapInMicroSec, jsonConfig, CN_interframetime);

    c_OutputCommon::SetConfig (jsonConfig);

    bool response = validate ();

    AdjustedBrightness = map (brightness, 0, 100, 0, 255);
    // DEBUG_V (String ("brightness: ") + String (brightness));
    // DEBUG_V (String ("AdjustedBrightness: ") + String (AdjustedBrightness));

    updateGammaTable ();
    updateColorOrderOffsets ();

    // Update the config fields in case the validator changed them
    GetConfig (jsonConfig);

    ZigPixelCount = (2 > zig_size) ? pixel_count : zig_size;
    GroupPixelCount = (2 > group_size) ? 1 : group_size;

    // Calculate our refresh time
    FrameMinDurationInMicroSec = (WS2811_MICRO_SEC_PER_INTENSITY * OutputBufferSize) + InterFrameGapInMicroSec;

    // Calculate our refresh time
    FrameMinDurationInMicroSec = (WS2811_MICRO_SEC_PER_INTENSITY * OutputBufferSize) + InterFrameGapInMicroSec;

    // DEBUG_END;
    return response;

} // SetConfig

//----------------------------------------------------------------------------
void c_OutputWS2811::updateGammaTable ()
{
    // DEBUG_START;
    double tempBrightness = double (brightness) / 100.0;
    // DEBUG_V (String ("tempBrightness: ") + String (tempBrightness));

    for (int i = 0; i < sizeof (gamma_table); ++i)
    {
        // ESP.wdtFeed ();
        gamma_table[i] = (uint8_t)min ((255.0 * pow (i * tempBrightness / 255, gamma) + 0.5), 255.0);
        // DEBUG_V (String ("i: ") + String (i));
        // DEBUG_V (String ("gamma_table[i]: ") + String (gamma_table[i]));
    }

    // DEBUG_END;
} // updateGammaTable

//----------------------------------------------------------------------------
void c_OutputWS2811::updateColorOrderOffsets ()
{
    // DEBUG_START;
    // make sure the color order is all lower case
    color_order.toLowerCase ();

    if (String (F ("rgbw")) == color_order) { ColorOffsets.offset.r = 0; ColorOffsets.offset.g = 1; ColorOffsets.offset.b = 2; ColorOffsets.offset.w = 3; numIntensityBytesPerPixel = 4; }
    else if (String (F ("grbw")) == color_order) { ColorOffsets.offset.r = 1; ColorOffsets.offset.g = 0; ColorOffsets.offset.b = 2; ColorOffsets.offset.w = 3; numIntensityBytesPerPixel = 4; }
    else if (String (F ("brgw")) == color_order) { ColorOffsets.offset.r = 1; ColorOffsets.offset.g = 2; ColorOffsets.offset.b = 0; ColorOffsets.offset.w = 3; numIntensityBytesPerPixel = 4; }
    else if (String (F ("rbgw")) == color_order) { ColorOffsets.offset.r = 0; ColorOffsets.offset.g = 2; ColorOffsets.offset.b = 1; ColorOffsets.offset.w = 3; numIntensityBytesPerPixel = 4; }
    else if (String (F ("gbrw")) == color_order) { ColorOffsets.offset.r = 2; ColorOffsets.offset.g = 0; ColorOffsets.offset.b = 1; ColorOffsets.offset.w = 3; numIntensityBytesPerPixel = 4; }
    else if (String (F ("bgrw")) == color_order) { ColorOffsets.offset.r = 2; ColorOffsets.offset.g = 1; ColorOffsets.offset.b = 0; ColorOffsets.offset.w = 3; numIntensityBytesPerPixel = 4; }
    else if (String (F ("grb")) == color_order) { ColorOffsets.offset.r = 1; ColorOffsets.offset.g = 0; ColorOffsets.offset.b = 2; ColorOffsets.offset.w = 3; numIntensityBytesPerPixel = 3; }
    else if (String (F ("brg")) == color_order) { ColorOffsets.offset.r = 1; ColorOffsets.offset.g = 2; ColorOffsets.offset.b = 0; ColorOffsets.offset.w = 3; numIntensityBytesPerPixel = 3; }
    else if (String (F ("rbg")) == color_order) { ColorOffsets.offset.r = 0; ColorOffsets.offset.g = 2; ColorOffsets.offset.b = 1; ColorOffsets.offset.w = 3; numIntensityBytesPerPixel = 3; }
    else if (String (F ("gbr")) == color_order) { ColorOffsets.offset.r = 2; ColorOffsets.offset.g = 0; ColorOffsets.offset.b = 1; ColorOffsets.offset.w = 3; numIntensityBytesPerPixel = 3; }
    else if (String (F ("bgr")) == color_order) { ColorOffsets.offset.r = 2; ColorOffsets.offset.g = 1; ColorOffsets.offset.b = 0; ColorOffsets.offset.w = 3; numIntensityBytesPerPixel = 3; }
    else
    {
        color_order = F ("rgb");
        ColorOffsets.offset.r = 0;
        ColorOffsets.offset.g = 1;
        ColorOffsets.offset.b = 2;
        ColorOffsets.offset.w = 3;
        numIntensityBytesPerPixel = 3;
    } // default

    // DEBUG_END;
} // updateColorOrderOffsets

//----------------------------------------------------------------------------
/*
*   Validate that the current values meet our needs
*
*   needs
*       data set in the class elements
*   returns
*       true - no issues found
*       false - had an issue and had to fix things
*/
bool c_OutputWS2811::validate ()
{
    // DEBUG_START;
    bool response = true;

    if (zig_size > pixel_count)
    {
        LOG_PORT.println (String (F ("*** Requested ZigZag size count was too high. Setting to ")) + pixel_count + F (" ***"));
        zig_size = pixel_count;
        response = false;
    }

    // Default gamma value
    if (gamma <= 0)
    {
        gamma = 2.2;
        response = false;
    }

    // Max brightness value
    if (brightness > 100)
    {
        brightness = 100;
        // DEBUG_V (String ("brightness: ") + String (brightness));
        response = false;
    }

    // DEBUG_END;
    return response;

} // validate

//----------------------------------------------------------------------------
void IRAM_ATTR c_OutputWS2811::UpdateToNextPixel ()
{
    uint8_t* response = pNextIntensityToSend;

    do // once
    {
        if (0 == RemainingPixelCount)
        {
            // nothign left to send
            break;
        }

        // has the group completed?
        --CurrentGroupPixelCount;
        if (0 != CurrentGroupPixelCount)
        {
            // not finished with the group yet
            continue;
        }

        // refresh the group count
        CurrentGroupPixelCount = GroupPixelCount;

        --RemainingPixelCount;
        if (0 == RemainingPixelCount)
        {
            // FrameDoneCounter++;
            break;
        }

        // have we completed the forward traverse
        if (CurrentZigPixelCount)
        {
            --CurrentZigPixelCount;
            // not finished with the set yet.
            pNextIntensityToSend += numIntensityBytesPerPixel;
            continue;
        }

        if (CurrentZagPixelCount == ZigPixelCount)
        {
            // first backward pixel
            pNextIntensityToSend += numIntensityBytesPerPixel * (ZigPixelCount + 1);
        }

        // have we completed the backward traverse
        if (CurrentZagPixelCount)
        {
            --CurrentZagPixelCount;
            // not finished with the set yet.
            pNextIntensityToSend -= numIntensityBytesPerPixel;
            continue;
        }

        // move to next forward pixel
        pNextIntensityToSend += numIntensityBytesPerPixel * (ZigPixelCount);

        // refresh the zigZag
        CurrentZigPixelCount = ZigPixelCount - 1;
        CurrentZagPixelCount = ZigPixelCount;

    } while (false);

} // UpdateToNextPixel
