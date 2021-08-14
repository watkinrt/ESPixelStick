#pragma once
/*
* OutputWS2811Rmt.h - WS2811 driver code for ESPixelStick RMT Channel
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
*   This is a derived class that converts data in the output buffer into
*   pixel intensities and then transmits them through the configured serial
*   interface.
*
*/
#ifdef ARDUINO_ARCH_ESP32

#include "OutputCommon.hpp"
#include "OutputWS2811.hpp"

#include <driver/rmt.h>

class c_OutputWS2811Rmt : public c_OutputWS2811
{
public:
    // These functions are inherited from c_OutputCommon
    c_OutputWS2811Rmt (c_OutputMgr::e_OutputChannelIds OutputChannelId,
                      gpio_num_t outputGpio,
                      uart_port_t uart,
                      c_OutputMgr::e_OutputType outputType);
    virtual ~c_OutputWS2811Rmt ();

    // functions to be provided by the derived class
    void    Begin ();                                         ///< set up the operating environment based on the current config (or defaults)
    bool    SetConfig (ArduinoJson::JsonObject& jsonConfig);  ///< Set a new config in the driver
    void    Render ();                                        ///< Call from loop(),  renders output data
    void    GetStatus (ArduinoJson::JsonObject& jsonStatus);

    /// Interrupt Handler
    void IRAM_ATTR ISR_Handler (); ///< ISR
    void IRAM_ATTR ISR_Handler_SendIntensityData (); ///< ISR
    void IRAM_ATTR ISR_Handler_StartNewFrame (); ///< ISR
    
    volatile rmt_item32_t * RmtStartAddr   = nullptr;
    volatile rmt_item32_t * RmtCurrentAddr = nullptr;
    volatile rmt_item32_t * RmtEndAddr     = nullptr;
    intr_handle_t RMT_intr_handle = NULL;
    uint8_t NumPixelsPerInterrupt = 2;
    rmt_item32_t Rgb2Rmt[5];

    uint32_t FrameStartCounter = 0;
    // uint32_t DataISRcounter = 0;
    // uint32_t FrameDoneCounter = 0;
    // uint32_t FrameEndISRcounter = 0;

}; // c_OutputWS2811Rmt

#endif // def ARDUINO_ARCH_ESP32
