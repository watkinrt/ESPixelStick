/*
* WiFiMgr.cpp - Output Management class
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
*/

#include "ESPixelStick.h"

#ifdef ARDUINO_ARCH_ESP8266
#   include <eagle_soc.h>
#   include <ets_sys.h>
#   include <SPI.h>
#   include <Ethernet.h>
#else
#   include <esp_wifi.h>
#   include <ETH.h>
#endif // def ARDUINO_ARCH_ESP8266

#include "WiFiMgr.hpp"
#include "input/InputMgr.hpp"

//-----------------------------------------------------------------------------
// Create secrets.h with a #define for SECRETS_SSID and SECRETS_PASS
// or delete the #include and enter the strings directly below.
#include "secrets.h"
#ifndef SECRETS_SSID
#   define SECRETS_SSID "DEFAULT_SSID_NOT_SET"
#   define SECRETS_PASS "DEFAULT_PASSPHRASE_NOT_SET"
#endif // ndef SECRETS_SSID

/* Fallback configuration if config->json is empty or fails */
const String ssid       = SECRETS_SSID;
const String passphrase = SECRETS_PASS;

// Ethernet connection status as ETH doesn't currently have a status function
static bool eth_connected = false;

#ifndef ARDUINO_ARCH_ESP8266
void EthTrackingEvent(WiFiEvent_t event)
{
  switch (event) {
    case SYSTEM_EVENT_ETH_START:
      Serial.println("ETH Started");
      //set eth hostname here
    //   ETH.setHostname("esp32-ethernet");
      break;
    case SYSTEM_EVENT_ETH_CONNECTED:
      Serial.println("ETH Connected");
      break;
    case SYSTEM_EVENT_ETH_GOT_IP:
      Serial.print("ETH MAC: ");
      Serial.print(ETH.macAddress());
      Serial.print(", IPv4: ");
      Serial.print(ETH.localIP());
      if (ETH.fullDuplex()) {
        Serial.print(", FULL_DUPLEX");
      }
      Serial.print(", ");
      Serial.print(ETH.linkSpeed());
      Serial.println("Mbps");
      eth_connected = true;
      break;
    case SYSTEM_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH Disconnected");
      eth_connected = false;
      break;
    case SYSTEM_EVENT_ETH_STOP:
      Serial.println("ETH Stopped");
      eth_connected = false;
      break;
    default:
      break;
  }
}
#endif
/// Radio configuration
/** ESP8266 radio configuration routines that are executed at startup. */
/* Disabled for now, possible flash wear issue. Need to research further
RF_PRE_INIT() {
#ifdef ARDUINO_ARCH_ESP8266
    system_phy_set_powerup_option(3);   // Do full RF calibration on power-up
    system_phy_set_max_tpw(82);         // Set max TX power
#else
    esp_phy_erase_cal_data_in_nvs(); // Do full RF calibration on power-up
    esp_wifi_set_max_tx_power (78);  // Set max TX power
#endif
}
*/

/*****************************************************************************/
/* FSM                                                                       */
/*****************************************************************************/
fsm_WiFi_state_Boot                    fsm_WiFi_state_Boot_imp;
fsm_WiFi_state_ConnectingToEthUsingConfig   fsm_WiFi_state_ConnectingToEthUsingConfig_imp;
fsm_WiFi_state_ConnectingToEthUsingDefaults fsm_WiFi_state_ConnectingToEthDefault_imp;
fsm_WiFi_state_ConnectingUsingConfig   fsm_WiFi_state_ConnectingUsingConfig_imp;
fsm_WiFi_state_ConnectingUsingDefaults fsm_WiFi_state_ConnectingDefault_imp;
fsm_WiFi_state_ConnectedToEth          fsm_WiFi_state_ConnectedToEth_imp;
fsm_WiFi_state_ConnectedToAP           fsm_WiFi_state_ConnectedToAP_imp;
fsm_WiFi_state_ConnectingAsAP          fsm_WiFi_state_ConnectingAsAP_imp;
fsm_WiFi_state_ConnectedToSta          fsm_WiFi_state_ConnectedToSta_imp;
fsm_WiFi_state_ConnectionFailed        fsm_WiFi_state_ConnectionFailed_imp;

//-----------------------------------------------------------------------------
///< Start up the driver and put it into a safe mode
c_WiFiMgr::c_WiFiMgr ()
{
    // this gets called pre-setup so there is nothing we can do here.
    fsm_WiFi_state_Boot_imp.Init ();
} // c_WiFiMgr

//-----------------------------------------------------------------------------
///< deallocate any resources and put the output channels into a safe state
c_WiFiMgr::~c_WiFiMgr()
{
 // DEBUG_START;

 // DEBUG_END;

} // ~c_WiFiMgr

//-----------------------------------------------------------------------------
///< Start the module
void c_WiFiMgr::Begin (config_t* NewConfig)
{
    // DEBUG_START;

    // save the pointer to the config
    config = NewConfig;

    // Disable persistant credential storage and configure SDK params
    WiFi.persistent (false);

#ifdef ARDUINO_ARCH_ESP8266
    wifi_set_sleep_type (NONE_SLEEP_T);
#elif defined(ARDUINO_ARCH_ESP32)
    esp_wifi_set_ps (WIFI_PS_NONE);
#endif

    // DEBUG_V ("");

    // Setup WiFi Handlers
#ifdef ARDUINO_ARCH_ESP8266
    wifiConnectHandler    = WiFi.onStationModeGotIP        ([this](const WiFiEventStationModeGotIP& event) {this->onWiFiConnect (event); });
    wifiDisconnectHandler = WiFi.onStationModeDisconnected ([this](const WiFiEventStationModeDisconnected& event) {this->onWiFiDisconnect (event); });
#else
    WiFi.onEvent(EthTrackingEvent);
    WiFi.onEvent ([this](WiFiEvent_t event, system_event_info_t info) {this->onWiFiConnect    (event, info);}, WiFiEvent_t::SYSTEM_EVENT_ETH_GOT_IP);
    WiFi.onEvent ([this](WiFiEvent_t event, system_event_info_t info) {this->onWiFiDisconnect (event, info);}, WiFiEvent_t::SYSTEM_EVENT_ETH_DISCONNECTED);
    WiFi.onEvent ([this](WiFiEvent_t event, system_event_info_t info) {this->onWiFiStaConn (event, info); }, WiFiEvent_t::SYSTEM_EVENT_STA_CONNECTED);
    WiFi.onEvent ([this](WiFiEvent_t event, system_event_info_t info) {this->onWiFiStaDisc (event, info); }, WiFiEvent_t::SYSTEM_EVENT_STA_DISCONNECTED);

    WiFi.onEvent ([this](WiFiEvent_t event, system_event_info_t info) {this->onWiFiConnect    (event, info);}, WiFiEvent_t::SYSTEM_EVENT_STA_GOT_IP);
    WiFi.onEvent ([this](WiFiEvent_t event, system_event_info_t info) {this->onWiFiDisconnect (event, info);}, WiFiEvent_t::SYSTEM_EVENT_STA_DISCONNECTED);
#endif

    // set up the poll interval
    NextPollTime = millis () + PollInterval;

    // get the FSM moving
    pCurrentFsmState->Poll ();

    // DEBUG_END;

} // begin

//-----------------------------------------------------------------------------
void c_WiFiMgr::GetStatus (JsonObject & jsonStatus)
{
 // DEBUG_START;

    jsonStatus[CN_rssi]   = WiFi.RSSI ();
    jsonStatus[CN_ip]     = getIpAddress ().toString ();
    jsonStatus[CN_subnet] = getIpSubNetMask ().toString ();
    jsonStatus[CN_mac]    = WiFi.macAddress ();
#ifdef ARDUINO_ARCH_ESP8266
    jsonStatus[CN_hostname] = WiFi.hostname ();
#else
    jsonStatus[CN_hostname] = WiFi.getHostname ();
#endif // def ARDUINO_ARCH_ESP8266
    jsonStatus[CN_ssid]       = WiFi.SSID ();

 // DEBUG_END;
} // GetStatus

//-----------------------------------------------------------------------------
void c_WiFiMgr::connectEth ()
{
    // DEBUG_START;
    #ifndef ARDUINO_ARCH_ESP8266
    // disconnect just in case
// #ifdef ARDUINO_ARCH_ESP8266
//     return;
// #endif
    // DEBUG_V ("");

    // @TODO The ethernet setup currently runs against default hardware setup.
    // Rather than carry the configuration here, these defaults can be
    // overridden as -D statement upon compilation. This should be documented at
    // some point.
    // begin(uint8_t phy_addr=ETH_PHY_ADDR, 
    //       int power=ETH_PHY_POWER,
    //       int mdc=ETH_PHY_MDC, 
    //       int mdio=ETH_PHY_MDIO, 
    //       eth_phy_type_t type=ETH_PHY_TYPE,
    //       eth_clock_mode_t clk_mode=ETH_CLK_MODE);
    ETH.begin ();

    // DEBUG_V (String ("config->hostname: ") + config->hostname);
    if (0 != config->hostname.length ())
    {
        // DEBUG_V (String ("Setting WiFi hostname: ") + config->hostname);

        // ETH.config (INADDR_NONE, INADDR_NONE, INADDR_NONE);
        ETH.setHostname (config->hostname.c_str ());
    }
    WiFiMgr.setHostname(ETH.getHostname());

    LOG_PORT.println (String(F ("\nEthernet Connecting as ")) +
                      config->hostname);
    #endif

    // DEBUG_END;
} // connectEth

//-----------------------------------------------------------------------------
void c_WiFiMgr::connectWifi (const String & ssid, const String & passphrase)
{
    // DEBUG_START;

    // disconnect just in case
#ifdef ARDUINO_ARCH_ESP8266
    WiFi.disconnect ();
#else
    WiFi.persistent (false);
    // DEBUG_V ("");
    WiFi.disconnect (true);
#endif
    // DEBUG_V ("");

    // Switch to station mode
    WiFi.mode (WIFI_STA);
    // DEBUG_V ("");

    // DEBUG_V (String ("config->hostname: ") + config->hostname);
    if (0 != config->hostname.length ())
    {
        // DEBUG_V (String ("Setting WiFi hostname: ") + config->hostname);

#ifdef ARDUINO_ARCH_ESP8266
        WiFi.hostname (config->hostname);
#else
        WiFi.config (INADDR_NONE, INADDR_NONE, INADDR_NONE);
        WiFi.setHostname (config->hostname.c_str ());
#endif
    }
#ifdef ARDUINO_ARCH_ESP8266
    WiFiMgr.setHostname (WiFi.hostname());
#else
    WiFiMgr.setHostname (WiFi.getHostname());
#endif

    LOG_PORT.println (String(F ("\nWiFi Connecting to '")) +
                      ssid +
                      String (F ("' as ")) +
                      config->hostname);

    WiFi.begin (ssid.c_str (), passphrase.c_str ());

    // DEBUG_END;
} // connectWifi

//-----------------------------------------------------------------------------
void c_WiFiMgr::reset ()
{
    // DEBUG_START;

    LOG_PORT.println (F ("WiFi Reset has been requested"));

    fsm_WiFi_state_Boot_imp.Init ();
    if (IsWiFiConnected())
    {
        InputMgr.NetworkStateChanged (false);
    }

    // DEBUG_END;
} // reset

//-----------------------------------------------------------------------------
void c_WiFiMgr::SetUpEthIp ()
{
    // DEBUG_START;
    #ifndef ARDUINO_ARCH_ESP8266
    do // once
    {
        if (true == config->UseDhcp)
        {
            LOG_PORT.println (F ("Connected with DHCP"));
            break;
        }

        IPAddress temp = (uint32_t)0;
        // DEBUG_V ("   temp: " + temp.toString ());
        // DEBUG_V ("     ip: " + config->ip.toString());
        // DEBUG_V ("netmask: " + config->netmask.toString ());
        // DEBUG_V ("gateway: " + config->gateway.toString ());

        if (temp == config->ip)
        {
            LOG_PORT.println (F ("NETWORK: ERROR: STATIC SELECTED WITHOUT IP. Using DHCP assigned address"));
            break;
        }

        if ((config->ip      == ETH.localIP ())    &&
            (config->netmask == ETH.subnetMask ()) &&
            (config->gateway == ETH.gatewayIP ()))
        {
            // correct IP is already set
            break;
        }


        // We didn't use DNS, so just set it to our configured gateway
        ETH.config (config->ip, config->gateway, config->netmask, config->gateway);

        LOG_PORT.println (F ("Connected to Ethernet with Static IP"));

    } while (false);
    #endif
    // DEBUG_END;

} // SetUpIp

//-----------------------------------------------------------------------------
void c_WiFiMgr::SetUpIp ()
{
    // DEBUG_START;

    do // once
    {
        if (true == config->UseDhcp)
        {
            LOG_PORT.println (F ("Connected with DHCP"));
            break;
        }

        IPAddress temp = (uint32_t)0;
        // DEBUG_V ("   temp: " + temp.toString ());
        // DEBUG_V ("     ip: " + config->ip.toString());
        // DEBUG_V ("netmask: " + config->netmask.toString ());
        // DEBUG_V ("gateway: " + config->gateway.toString ());

        if (temp == config->ip)
        {
            LOG_PORT.println (F ("NETWORK: ERROR: STATIC SELECTED WITHOUT IP. Using DHCP assigned address"));
            break;
        }

        if ((config->ip      == WiFi.localIP ())    &&
            (config->netmask == WiFi.subnetMask ()) &&
            (config->gateway == WiFi.gatewayIP ()))
        {
            // correct IP is already set
            break;
        }


        // We didn't use DNS, so just set it to our configured gateway
        WiFi.config (config->ip, config->gateway, config->netmask, config->gateway);

        LOG_PORT.println (F ("Connected with Static IP"));

    } while (false);

    // DEBUG_END;

} // SetUpIp

//-----------------------------------------------------------------------------
#ifdef ARDUINO_ARCH_ESP32

//-----------------------------------------------------------------------------
void c_WiFiMgr::onWiFiStaConn (const WiFiEvent_t event, const WiFiEventInfo_t info)
{
    // DEBUG_V ("ESP has associated with the AP");
} // onWiFiStaConn

//-----------------------------------------------------------------------------
void c_WiFiMgr::onWiFiStaDisc (const WiFiEvent_t event, const WiFiEventInfo_t info)
{
    // DEBUG_V ("ESP has disconnected from the AP");
} // onWiFiStaDisc

#endif // def ARDUINO_ARCH_ESP32

//-----------------------------------------------------------------------------
#ifdef ARDUINO_ARCH_ESP8266
void c_WiFiMgr::onWiFiConnect (const WiFiEventStationModeGotIP& event)
{
#else
void c_WiFiMgr::onWiFiConnect (const WiFiEvent_t event, const WiFiEventInfo_t info)
{
#endif
    // DEBUG_START;

    // Check to see if WiFi is already connected. If so, restart manager. If
    // not, initialize connected state
#ifdef ARDUINO_ARCH_ESP8266
    if (false) {
#else
    if (WiFiMgr.IsWiFiConnected() && event == WiFiEvent_t::SYSTEM_EVENT_ETH_GOT_IP) {
#endif
        LOG_PORT.println (F ("Both network interfaces connected. Requesting Reboot"));
        //@TODO I'm not sure if this is the best way to handle this, but trying
        //to sort out the connections otherwise is somewhat involved. A reboot
        //seems like the easiest way to go about this.
        extern bool reboot;
        reboot = true;
    }
    else {
        pCurrentFsmState->OnConnect ();
    }

    

    // DEBUG_END;
} // onWiFiConnect


//-----------------------------------------------------------------------------
/// WiFi Disconnect Handler
#ifdef ARDUINO_ARCH_ESP8266
/** Attempt to re-connect every 2 seconds */
void c_WiFiMgr::onWiFiDisconnect (const WiFiEventStationModeDisconnected & event)
{
#else
void c_WiFiMgr::onWiFiDisconnect (const WiFiEvent_t event, const WiFiEventInfo_t info)
{
#endif
    // DEBUG_START;

    pCurrentFsmState->OnDisconnect ();

    // DEBUG_END;

} // onWiFiDisconnect

//-----------------------------------------------------------------------------
int c_WiFiMgr::ValidateConfig (config_t* NewConfig)
{
    // DEBUG_START;

    int response = 0;

    if (0 == NewConfig->ssid.length ())
    {
        NewConfig->ssid = ssid;
        // DEBUG_V ();
        response++;
    }

    if (0 == NewConfig->passphrase.length ())
    {
        NewConfig->passphrase = passphrase;
        // DEBUG_V ();
        response++;
    }

    if (NewConfig->sta_timeout < 5)
    {
        NewConfig->sta_timeout = CLIENT_TIMEOUT;
        // DEBUG_V ();
        response++;
    }

    if (NewConfig->ap_timeout < 15)
    {
        NewConfig->ap_timeout = AP_TIMEOUT;
        // DEBUG_V ();
        response++;
    }

    // DEBUG_END;

    return response;

} // ValidateConfig

//-----------------------------------------------------------------------------
void c_WiFiMgr::AnnounceState ()
{
    // DEBUG_START;

    String StateName;
    pCurrentFsmState->GetStateName (StateName);
    LOG_PORT.println (String (F ("\nNetwork Entering State: ")) + StateName);

    // DEBUG_END;

} // AnnounceState

//-----------------------------------------------------------------------------
void c_WiFiMgr::Poll ()
{
    // DEBUG_START;

    if (millis () > NextPollTime)
    {
        // DEBUG_V ("");
        NextPollTime += PollInterval;
        pCurrentFsmState->Poll ();
    }

    // DEBUG_END;

} // Poll

/*****************************************************************************/
//  FSM Code
/*****************************************************************************/
/*****************************************************************************/
// Waiting for polling to start
void fsm_WiFi_state_Boot::Poll ()
{
    // DEBUG_START;

    // Start trying to connect to based on input config
#ifdef ARDUINO_ARCH_ESP8266
    fsm_WiFi_state_ConnectingUsingConfig_imp.Init ();
#else
    fsm_WiFi_state_ConnectingToEthUsingConfig_imp.Init ();
#endif
    // DEBUG_END;
} // fsm_WiFi_state_boot

/*****************************************************************************/
// Waiting for polling to start
void fsm_WiFi_state_Boot::Init ()
{
    // DEBUG_START;

    WiFiMgr.SetFsmState( this );

    // This can get called before the system is up and running.
    // No log port available yet
    // WiFiMgr.AnnounceState ();

    // DEBUG_END;
} // fsm_WiFi_state_Boot::Init

/*****************************************************************************/
/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectingToEthUsingConfig::Poll ()
{
    // DEBUG_START;
    // DEBUG_V ("Eth config poll");
    // wait for the connection to complete via the callback function
    uint32_t CurrentTimeMS = millis ();

    // Check ethernet status first
    if (!eth_connected)
    {
        // @TODO Ethernet connection timeout is currently hardcoded to 10. Add
        // to network config.
        if (CurrentTimeMS - WiFiMgr.GetFsmStartTime() > (5000))
        {
            LOG_PORT.println (F ("\nEthernet Failed to connect using Configured Credentials"));
            fsm_WiFi_state_ConnectingToEthDefault_imp.Init ();
        }
    }

    // DEBUG_END;
} // fsm_WiFi_state_ConnectingToEthUsingConfig::Poll

/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectingToEthUsingConfig::Init ()
{
    // DEBUG_START;
    // DEBUG_V ("Eth config init");
    WiFiMgr.SetFsmState (this);
    WiFiMgr.AnnounceState ();
    WiFiMgr.SetFsmStartTime (millis ());

    // First try to connect to ethernet followed by WiFi
    WiFiMgr.connectEth ();

    //@TODO This is currently a hack. I think we need to initialize the WiFi to
    //other things down the line to work properly, but I don't fully understand
    //this yet.
    // WiFiMgr.connectWifi ();

    // DEBUG_END;

} // fsm_WiFi_state_ConnectingToEthUsingConfig::Init

/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectingToEthUsingConfig::OnConnect ()
{
    // DEBUG_START;

    fsm_WiFi_state_ConnectedToEth_imp.Init ();

    // DEBUG_END;

} // fsm_WiFi_state_ConnectingToEthUsingDefaults::OnConnect

/*****************************************************************************/
/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectingToEthUsingDefaults::Poll ()
{
    // DEBUG_START;
    // DEBUG_V ("Eth default poll");
    // wait for the connection to complete via the callback function
    uint32_t CurrentTimeMS = millis ();

    // Check ethernet status first
    if (! eth_connected) {
        // @TODO Ethernet connection timeout is currently hardcoded to 1s. Add
        // to network config.
        if (CurrentTimeMS - WiFiMgr.GetFsmStartTime() > (1000))
        {
            LOG_PORT.println (F ("\nEthernet Failed to connect using default Credentials"));
            fsm_WiFi_state_ConnectingUsingConfig_imp.Init ();
        }
    }

    // DEBUG_END;
} // fsm_WiFi_state_ConnectingToEthUsingDefaults::Poll

/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectingToEthUsingDefaults::Init ()
{
    // DEBUG_START;

    WiFiMgr.SetFsmState (this);
    WiFiMgr.AnnounceState ();
    WiFiMgr.SetFsmStartTime (millis ());

    // Switch to station mode and disconnect just in case
    // DEBUG_V ("");

    // First try to connect to ethernet followed by WiFi
    WiFiMgr.connectEth ();

    // DEBUG_END;
} // fsm_WiFi_state_ConnectingToEthUsingDefaults::Init

/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectingToEthUsingDefaults::OnConnect ()
{
    // DEBUG_START;

    fsm_WiFi_state_ConnectedToEth_imp.Init ();

    // DEBUG_END;

} // fsm_WiFi_state_ConnectingToEthUsingDefaults::OnConnect


/*****************************************************************************/
/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectingUsingConfig::Poll ()
{
    // DEBUG_START;

    // wait for the connection to complete via the callback function
    uint32_t CurrentTimeMS = millis ();

    // If ethernet isn't connected, check WiFi
    if (WiFi.status () != WL_CONNECTED)
    {
        if (CurrentTimeMS - WiFiMgr.GetFsmStartTime() > (1000 * WiFiMgr.GetConfigPtr()->sta_timeout))
        {
            LOG_PORT.println (F ("\nWiFi Failed to connect using Configured Credentials"));
            fsm_WiFi_state_ConnectingDefault_imp.Init ();
        }
    }

    // DEBUG_END;
} // fsm_WiFi_state_ConnectingUsingConfig::Poll

/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectingUsingConfig::Init ()
{
    // DEBUG_START;

    if ((0 == config.ssid.length ()) || (String("null") == config.ssid))
    {
        fsm_WiFi_state_ConnectingDefault_imp.Init ();
    }
    else
    {
        WiFiMgr.SetFsmState (this);
        WiFiMgr.AnnounceState ();
        WiFiMgr.SetFsmStartTime (millis ());

        WiFiMgr.connectWifi (config.ssid, config.passphrase);
    }

    // DEBUG_END;

} // fsm_WiFi_state_ConnectingUsingConfig::Init

/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectingUsingConfig::OnConnect ()
{
    // DEBUG_START;

    fsm_WiFi_state_ConnectedToAP_imp.Init ();

    // DEBUG_END;

} // fsm_WiFi_state_ConnectingUsingConfig::OnConnect

/*****************************************************************************/
/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectingUsingDefaults::Poll ()
{
    // DEBUG_START;

    // wait for the connection to complete via the callback function
    uint32_t CurrentTimeMS = millis ();

    if (WiFi.status () != WL_CONNECTED)
    {
        if (CurrentTimeMS - WiFiMgr.GetFsmStartTime () > (1000 * WiFiMgr.GetConfigPtr ()->sta_timeout))
        {
            LOG_PORT.println (F ("\nWiFi Failed to connect using default Credentials"));
            fsm_WiFi_state_ConnectingAsAP_imp.Init ();
        }
    }

    // DEBUG_END;
} // fsm_WiFi_state_ConnectingUsingDefaults::Poll

/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectingUsingDefaults::Init ()
{
    // DEBUG_START;

    WiFiMgr.SetFsmState (this);
    WiFiMgr.AnnounceState ();
    WiFiMgr.SetFsmStartTime (millis ());

    // Switch to station mode and disconnect just in case
    // DEBUG_V ("");

    WiFiMgr.connectWifi (ssid, passphrase);

    // DEBUG_END;
} // fsm_WiFi_state_ConnectingUsingDefaults::Init

/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectingUsingDefaults::OnConnect ()
{
    // DEBUG_START;

    fsm_WiFi_state_ConnectedToAP_imp.Init ();

    // DEBUG_END;

} // fsm_WiFi_state_ConnectingUsingDefaults::OnConnect

/*****************************************************************************/
/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectingAsAP::Poll ()
{
    // DEBUG_START;

    if (0 != WiFi.softAPgetStationNum ())
    {
        fsm_WiFi_state_ConnectedToSta_imp.Init();
    }
    else
    {
        LOG_PORT.print (".");

        if (millis () - WiFiMgr.GetFsmStartTime () > (1000 * WiFiMgr.GetConfigPtr ()->ap_timeout))
        {
            LOG_PORT.println (F ("\nWiFi STA Failed to connect"));
            fsm_WiFi_state_ConnectionFailed_imp.Init ();
        }
    }

    // DEBUG_END;
} // fsm_WiFi_state_ConnectingAsAP::Poll

/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectingAsAP::Init ()
{
    // DEBUG_START;

    WiFiMgr.SetFsmState (this);
    WiFiMgr.AnnounceState ();

    if (true == WiFiMgr.GetConfigPtr ()->ap_fallbackIsEnabled)
    {
        WiFi.mode (WIFI_AP);

        String ssid = "ESPixelStick " + String (WiFiMgr.GetConfigPtr ()->hostname);
        WiFi.softAP (ssid.c_str ());

        WiFiMgr.setIpAddress (WiFi.localIP ());
        WiFiMgr.setIpSubNetMask (WiFi.subnetMask ());
        WiFiMgr.setMacAddress (WiFi.macAddress());

        LOG_PORT.println (String (F ("WiFi SOFTAP: IP Address: '")) + WiFiMgr.getIpAddress().toString ());
    }
    else
    {
        LOG_PORT.println (String (F ("WiFi SOFTAP: Not enabled")));
        fsm_WiFi_state_ConnectionFailed_imp.Init ();
    }

    // DEBUG_END;

} // fsm_WiFi_state_ConnectingAsAP::Init

/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectingAsAP::OnConnect ()
{
 // DEBUG_START;

    fsm_WiFi_state_ConnectedToSta_imp.Init ();

 // DEBUG_END;

} // fsm_WiFi_state_ConnectedToEth::OnConnect

/*****************************************************************************/
/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectedToEth::Poll ()
{
    // DEBUG_START;

    // did we get silently disconnected?
    if (!eth_connected)
    {
     // DEBUG_V ("WiFi Handle Silent Disconnect");
        fsm_WiFi_state_ConnectedToEth_imp.OnDisconnect ();
    }

    // DEBUG_END;
} // fsm_WiFi_state_ConnectedToEth::Poll

/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectedToEth::Init ()
{
#ifndef ARDUINO_ARCH_ESP8266
    // DEBUG_START;
    WiFiMgr.SetFsmState (this);
    WiFiMgr.AnnounceState ();

    WiFiMgr.SetUpEthIp ();

    WiFiMgr.setIpAddress( ETH.localIP () );
    WiFiMgr.setIpSubNetMask( ETH.subnetMask () );
    WiFiMgr.setMacAddress (ETH.macAddress());

    LOG_PORT.println (String (F ("Ethernet Connected with IP: ")) + WiFiMgr.getIpAddress ().toString ());

    WiFiMgr.SetIsEthConnected (true);
    InputMgr.NetworkStateChanged (true);
#endif
    // DEBUG_END;
} // fsm_WiFi_state_ConnectedToEth::Init

/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectedToEth::OnDisconnect ()
{
    // DEBUG_START;

    LOG_PORT.println (F ("Ethernet lost the connection"));
    fsm_WiFi_state_ConnectionFailed_imp.Init ();

    // DEBUG_END;

} // fsm_WiFi_state_ConnectedToEth::OnDisconnect

/*****************************************************************************/
/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectedToAP::Poll ()
{
    // DEBUG_START;

    // did we get silently disconnected?
    if (WiFi.status () != WL_CONNECTED)
    {
     // DEBUG_V ("WiFi Handle Silent Disconnect");
        WiFi.reconnect ();
    }

    // DEBUG_END;
} // fsm_WiFi_state_ConnectingAsAP::Poll

/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectedToAP::Init ()
{
    // DEBUG_START;

    WiFiMgr.SetFsmState (this);
    WiFiMgr.AnnounceState ();

    WiFiMgr.SetUpIp ();

    WiFiMgr.setIpAddress( WiFi.localIP () );
    WiFiMgr.setIpSubNetMask( WiFi.subnetMask () );
    WiFiMgr.setMacAddress (WiFi.macAddress());

    LOG_PORT.println (String (F ("WiFi Connected with IP: ")) + WiFiMgr.getIpAddress ().toString ());

    WiFiMgr.SetIsWiFiConnected (true);
    InputMgr.NetworkStateChanged (true);

    // DEBUG_END;
} // fsm_WiFi_state_ConnectingAsAP::Init

/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectedToAP::OnDisconnect ()
{
    // DEBUG_START;

    LOG_PORT.println (F ("WiFi Lost the connection to the AP"));
    fsm_WiFi_state_ConnectionFailed_imp.Init ();

    // DEBUG_END;

} // fsm_WiFi_state_ConnectedToAP::OnDisconnect

/*****************************************************************************/
/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectedToSta::Poll ()
{
   // DEBUG_START;

    // did we get silently disconnected?
    if (0 == WiFi.softAPgetStationNum ())
    {
        LOG_PORT.println (F ("WiFi Lost the connection to the STA"));
        fsm_WiFi_state_ConnectionFailed_imp.Init ();
    }

    // DEBUG_END;
} // fsm_WiFi_state_ConnectedToSta::Poll

/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectedToSta::Init ()
{
    // DEBUG_START;

    WiFiMgr.SetFsmState (this);
    WiFiMgr.AnnounceState ();

    WiFiMgr.SetUpIp ();

    WiFiMgr.setIpAddress (WiFi.softAPIP ());
    WiFiMgr.setIpSubNetMask (IPAddress (255, 255, 255, 0));
    WiFiMgr.setMacAddress (WiFi.macAddress());

    LOG_PORT.println (String (F ("\nWiFi Connected to STA with IP: ")) + WiFiMgr.getIpAddress ().toString ());

    WiFiMgr.SetIsWiFiConnected (true);
    InputMgr.NetworkStateChanged (true);


    // DEBUG_END;
} // fsm_WiFi_state_ConnectedToSta::Init

/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectedToSta::OnDisconnect ()
{
    // DEBUG_START;

    LOG_PORT.println (F ("WiFi STA Disconnected"));
    fsm_WiFi_state_ConnectionFailed_imp.Init ();

    // DEBUG_END;

} // fsm_WiFi_state_ConnectedToSta::OnDisconnect

/*****************************************************************************/
/*****************************************************************************/
// Wait for events
void fsm_WiFi_state_ConnectionFailed::Init ()
{
 // DEBUG_START;

    WiFiMgr.SetFsmState (this);
    WiFiMgr.AnnounceState ();

    if (WiFiMgr.IsWiFiConnected())
    {
        WiFiMgr.SetIsWiFiConnected (false);
        InputMgr.NetworkStateChanged (false);
    }
    else
    {
        if (true == WiFiMgr.GetConfigPtr ()->RebootOnWiFiFailureToConnect)
        {
            extern bool reboot;
            LOG_PORT.println (F ("WiFi Requesting Reboot"));

            reboot = true;
        }
        else
        {
            LOG_PORT.println (F ("WiFi Reboot Disabled."));

            // start over
            fsm_WiFi_state_Boot_imp.Init ();
        }
    }

 // DEBUG_END;

} // fsm_WiFi_state_ConnectionFailed::Init
//-----------------------------------------------------------------------------

// create a global instance of the WiFi Manager
c_WiFiMgr WiFiMgr;
