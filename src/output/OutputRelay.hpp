#pragma once
/******************************************************************
*
*       Project: ESPixelStick - An ESP8266 / ESP32 and E1.31 based pixel (And Serial!) driver
*       Orginal ESPixelStickproject by 2015 Shelby Merrick
*
*       Brought to you by:
*              Bill Porter
*              www.billporter.info
*
*       See Readme for other info and version history
*
*
*This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or(at your option) any later version.
This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
<http://www.gnu.org/licenses/>
*
*This work is licensed under the Creative Commons Attribution-ShareAlike 3.0 Unported License.
*To view a copy of this license, visit http://creativecommons.org/licenses/by-sa/3.0/ or
*send a letter to Creative Commons, 444 Castro Street, Suite 900, Mountain View, California, 94041, USA.
******************************************************************
*
*   This is a derived class that converts data in the output buffer into 
*   Relay States and then outputs on a GPIO
*
*/

#include "OutputCommon.hpp"

class c_OutputRelay : public c_OutputCommon  
{
public:
    typedef struct RelayChannel_s
    {
        bool        Enabled;
        bool        InvertOutput;
        uint8_t     OnOffTriggerLevel;
        gpio_num_t  GpioId;
        uint8_t     OnValue;
        uint8_t     OffValue;
        uint8_t     previousValue;

    } RelayChannel_t;

    // These functions are inherited from c_OutputCommon
    c_OutputRelay (c_OutputMgr::e_OutputChannelIds OutputChannelId, 
                   gpio_num_t outputGpio, 
                   uart_port_t uart,
                   c_OutputMgr::e_OutputType outputType);
    virtual ~c_OutputRelay ();

    // functions to be provided by the derived class
    void Begin ();                                         ///< set up the operating environment based on the current config (or defaults)
    bool SetConfig (ArduinoJson::JsonObject & jsonConfig); ///< Set a new config in the driver
    void GetConfig (ArduinoJson::JsonObject & jsonConfig); ///< Get the current config used by the driver
    void Render ();                                        ///< Call from loop(),  renders output data
    void GetDriverName (String& sDriverName);
    void GetStatus (ArduinoJson::JsonObject & jsonStatus) { c_OutputCommon::GetStatus (jsonStatus); }
    uint16_t GetNumChannelsNeeded () { return Num_Channels; }

private:
#   define OM_RELAY_CHANNEL_LIMIT           8
#   define OM_RELAY_UPDATE_INTERVAL_NAME    CN_updateinterval
#   define OM_RELAY_CHANNEL_ENABLED_NAME    CN_en
#   define OM_RELAY_CHANNEL_INVERT_NAME     CN_inv

    bool    validate ();

    // config data
    RelayChannel_t  OutputList[OM_RELAY_CHANNEL_LIMIT];
    uint16_t        UpdateInterval = 0;

    // non config data
    String      OutputName;
    uint16_t    Num_Channels = OM_RELAY_CHANNEL_LIMIT;

}; // c_OutputRelay

