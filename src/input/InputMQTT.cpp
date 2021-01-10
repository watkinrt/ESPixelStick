/*
* InputMQTT.cpp
*
* Project: ESPixelStick - An ESP8266 / ESP32 and E1.31 based pixel driver
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

#include <ArduinoJson.h>
#include "../ESPixelStick.h"
#include <Ticker.h>
#include <Int64String.h>
#include "InputMQTT.h"
#include "InputEffectEngine.hpp"
#include "../WiFiMgr.hpp"

#if defined ARDUINO_ARCH_ESP32
#   include <functional>
#endif

//-----------------------------------------------------------------------------
c_InputMQTT::c_InputMQTT (
    c_InputMgr::e_InputChannelIds NewInputChannelId,
    c_InputMgr::e_InputType       NewChannelType,
    uint8_t                     * BufferStart,
    uint16_t                      BufferSize) :
    c_InputCommon (NewInputChannelId, NewChannelType, BufferStart, BufferSize)

{
    // DEBUG_START;

    pEffectsEngine = new c_InputEffectEngine (c_InputMgr::e_InputChannelIds::InputChannelId_1, c_InputMgr::e_InputType::InputType_Effects, InputDataBuffer, InputDataBufferSize);
    pEffectsEngine->SetOperationalState (false);

    // DEBUG_END;
} // c_InputE131

//-----------------------------------------------------------------------------
c_InputMQTT::~c_InputMQTT ()
{
    mqtt.unsubscribe (topic.c_str ());
    mqtt.disconnect (/*force = */ true);
    mqttTicker.detach ();

    // allow the other input channels to run
    InputMgr.SetOperationalState (true);

    if (nullptr != pEffectsEngine)
    {
        delete pEffectsEngine;
        pEffectsEngine = nullptr;
    }
} // ~c_InputMQTT

//-----------------------------------------------------------------------------
void c_InputMQTT::Begin()
{
    // DEBUG_START;

    Serial.println (String (F ("** 'MQTT' Initialization for input: '")) + InputChannelId + String (F ("' **")));

    if (true == HasBeenInitialized)
    {
        // DEBUG_END;
        return;
    }
    HasBeenInitialized = true;

    pEffectsEngine->Begin ();

    RegisterWithMqtt ();

    // DEBUG_END;

} // begin

//-----------------------------------------------------------------------------
void c_InputMQTT::GetConfig (JsonObject & jsonConfig)
{
    // DEBUG_START;

    // Serialize Config
    jsonConfig["ip"]       = ip;
    jsonConfig["port"]     = port;
    jsonConfig["user"]     = user;
    jsonConfig["password"] = password;
    jsonConfig["topic"]    = topic;
    jsonConfig["clean"]    = clean;
    jsonConfig["hadisco"]  = hadisco;
    jsonConfig["haprefix"] = haprefix;
    jsonConfig["lwt"]      = lwt;

    pEffectsEngine->GetConfig (jsonConfig);

    // DEBUG_END;

} // GetConfig

//-----------------------------------------------------------------------------
void c_InputMQTT::GetStatus (JsonObject& /* jsonStatus */)
{
    // DEBUG_START;

    // JsonObject mqttStatus = jsonStatus.createNestedObject (F ("mqtt"));
    // mqttStatus["unifirst"] = startUniverse;

    // DEBUG_END;

} // GetStatus

//-----------------------------------------------------------------------------
void c_InputMQTT::Process ()
{
    // DEBUG_START;
    // ignoring IsInputChannelActive

    pEffectsEngine->Process ();

    // DEBUG_END;

} // process

//-----------------------------------------------------------------------------
void c_InputMQTT::SetBufferInfo (uint8_t* BufferStart, uint16_t BufferSize)
{
    InputDataBuffer = BufferStart;
    InputDataBufferSize = BufferSize;

    pEffectsEngine->SetBufferInfo (BufferStart, BufferSize);

} // SetBufferInfo

//-----------------------------------------------------------------------------
boolean c_InputMQTT::SetConfig (ArduinoJson::JsonObject& jsonConfig)
{
    // DEBUG_START;

    disconnectFromMqtt ();

    setFromJSON (ip,       jsonConfig, F ("ip"));
    setFromJSON (port,     jsonConfig, F ("port"));
    setFromJSON (user,     jsonConfig, F ("user"));
    setFromJSON (password, jsonConfig, F ("password"));
    setFromJSON (topic,    jsonConfig, F ("topic"));
    setFromJSON (clean,    jsonConfig, F ("clean"));
    setFromJSON (hadisco,  jsonConfig, F ("hadisco"));
    setFromJSON (haprefix, jsonConfig, F ("haprefix"));
    setFromJSON (lwt,      jsonConfig, F ("lwt"));

    pEffectsEngine->SetConfig (jsonConfig);

    validateConfiguration ();

    // Update the config fields in case the validator changed them
    GetConfig (jsonConfig);

    RegisterWithMqtt ();
    connectToMqtt ();

    // DEBUG_END;
    return true;
} // SetConfig

//-----------------------------------------------------------------------------
//TODO: Add MQTT configuration validation
void c_InputMQTT::validateConfiguration ()
{
    // DEBUG_START;
    // DEBUG_END;

} // validate

/////////////////////////////////////////////////////////
//
//  MQTT Section
//
/////////////////////////////////////////////////////////

void c_InputMQTT::RegisterWithMqtt ()
{
    using namespace std::placeholders;
    mqtt.onConnect (std::bind (&c_InputMQTT::onMqttConnect, this, _1));
    mqtt.onDisconnect (std::bind (&c_InputMQTT::onMqttDisconnect, this, _1));
    mqtt.onMessage (std::bind (&c_InputMQTT::onMqttMessage, this, _1, _2, _3, _4, _5, _6));
    mqtt.setServer (ip.c_str (), port);

    // Unset clean session (defaults to true) so we get retained messages of QoS > 0
    mqtt.setCleanSession (clean);
    if (user.length () > 0)
    {
        mqtt.setCredentials (user.c_str (), password.c_str ());
    }

} // RegisterWithMqtt

//-----------------------------------------------------------------------------
void c_InputMQTT::onConnect()
{
    // DEBUG_START;

    connectToMqtt();

    // DEBUG_END;

} // onConnect

//-----------------------------------------------------------------------------
void c_InputMQTT::onDisconnect()
{
    // DEBUG_START;

    mqttTicker.detach();

    // DEBUG_END;

} // onDisconnect

//-----------------------------------------------------------------------------
void c_InputMQTT::update()
{
    // DEBUG_START;

    // Update Home Assistant Discovery if enabled
    publishHA ();
    publishState ();

    // DEBUG_END;

} // update

//-----------------------------------------------------------------------------
void c_InputMQTT::connectToMqtt()
{
    // DEBUG_START;

    LOG_PORT.print(F("- Connecting to MQTT Broker "));
    LOG_PORT.println(ip);
    mqtt.connect();
    mqtt.setWill (topic.c_str(), 1, true, lwt.c_str(), lwt.length());

    // DEBUG_END;
} // connectToMqtt

//-----------------------------------------------------------------------------
void c_InputMQTT::disconnectFromMqtt ()
{
    // DEBUG_START;

    LOG_PORT.println (F ("- Disconnecting from MQTT Broker "));
    mqtt.disconnect ();

    // DEBUG_END;
} // disconnectFromMqtt

//-----------------------------------------------------------------------------
void c_InputMQTT::onMqttConnect(bool sessionPresent)
{
    // DEBUG_START;

    LOG_PORT.println(F("- MQTT Connected"));

    // Get retained MQTT state
    mqtt.subscribe(topic.c_str(), 0);
    mqtt.unsubscribe(topic.c_str());

    // Setup subscriptions
    mqtt.subscribe(String(topic + SET_COMMAND_TOPIC).c_str(), 0);

    // Publish state
    update ();

    // DEBUG_END;

} // onMqttConnect

//-----------------------------------------------------------------------------
// void c_InputMQTT::onMqttDisconnect (AsyncMqttClientDisconnectReason reason) {
void c_InputMQTT::onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
    // DEBUG_START;

    LOG_PORT.println(F("- MQTT Disconnected"));
    if (WiFiMgr.IsWiFiConnected () | WiFiMgr.IsEthConnected ())
    {
        // set up a two second delayed action.
        mqttTicker.once (2, +[](c_InputMQTT* pMe) { pMe->connectToMqtt (); }, this);
    }
} // onMqttDisconnect

//-----------------------------------------------------------------------------
void c_InputMQTT::onMqttMessage(
    char* topic,
    char* payload,
    AsyncMqttClientMessageProperties properties,
    size_t len,
    size_t index,
    size_t total)
{
    // DEBUG_START;
    do // once
    {
        // DEBUG_V (String("payload: ") + String(payload));

        if ('{' != payload[0])
        {
            // not a json payload.
            break;
        }

        DynamicJsonDocument r (1024);
        DeserializationError error = deserializeJson (r, payload, len);

        // DEBUG_V ("");
        if (error)
        {
            LOG_PORT.println (String (F ("MQTT: Deserialzation Error. Error code = ")) + error.c_str ());
            break;
        }

        JsonObject root = r.as<JsonObject> ();
        // DEBUG_V ("");

        // if its a retained message and we want clean session, ignore it
        if (properties.retain && clean)
        {
            // DEBUG_V ("");
            break;
        }

        // DEBUG_V ("");

        if (root.containsKey ("state"))
        {
            // DEBUG_V ("");
            if (strcmp (root["state"], ON) == 0)
            {
                // DEBUG_V ("");
                stateOn = true;
                // blank the other input channels
                InputMgr.SetOperationalState (false);
                // DEBUG_V ("");
                pEffectsEngine->SetOperationalState (true);

                // DEBUG_V ("");
            }
            else if (strcmp (root["state"], OFF) == 0)
            {
                // DEBUG_V ("");
                stateOn = false;

                // allow the other input channels to run
                InputMgr.SetOperationalState (true);
                // DEBUG_V ("");
                pEffectsEngine->SetOperationalState (false);
                // DEBUG_V ("");
            }
        }
        // DEBUG_V ("");

        ((c_InputEffectEngine*)(pEffectsEngine))->SetMqttConfig (root);
        // DEBUG_V ("");

        publishState ();
        // DEBUG_V ("");
    } while (false);

    // DEBUG_END;

} // onMqttMessage

//-----------------------------------------------------------------------------
void c_InputMQTT::publishHA()
{
    // DEBUG_START;

    // Setup HA discovery
#ifdef ARDUINO_ARCH_ESP8266
    String chipId = String (ESP.getChipId (), HEX);
#else
    String chipId = int64String (ESP.getEfuseMac (), HEX);
#endif
    String ha_config = haprefix + "/light/" + chipId + "/config";

    // DEBUG_V (String ("ha_config: ") + ha_config);
    // DEBUG_V (String ("hadisco: ") + hadisco);

    if (hadisco)
    {
        // DEBUG_V ("");
        DynamicJsonDocument root(1024);
        JsonObject JsonConfig = root.to<JsonObject> ();

//TODO: Fix - how to reference config.id from here? Pass in pointer to cfg struct from main?
        JsonConfig["platform"]      = "MQTT";
        JsonConfig["name"]          = "MartinFixMe";
        JsonConfig["schema"]        = "json";
        JsonConfig["state_topic"]   = topic;
        JsonConfig["command_topic"] = topic + "/set";
        JsonConfig["rgb"]           = "true";
        JsonConfig["brightness"]    = "true";
        JsonConfig["effect"]        = "true";

        ((c_InputEffectEngine*)(pEffectsEngine))->GetMqttEffectList (JsonConfig);

        // Register the attributes topic
        JsonConfig["json_attributes_topic"] = topic + "/attributes";

        // Create a unique id using the chip id, and fill in the device properties
        // to enable integration support in HomeAssistant.
        JsonConfig["unique_id"] = "ESPixelStick_" + chipId;

        JsonObject device = JsonConfig.createNestedObject (DEVICE_NAME);
        device["identifiers"]  = WiFiMgr.getMacAddress ();
        device["manufacturer"] = "ESPixelStick";
        device["model"]        = "Pixel Controller";
//TODO: Fix this
        device["name"]         = "MartinFixMe";
        device["sw_version"]   = "ESPixelStick v" + String (VERSION);

        String HaJsonConfig;
        serializeJson(JsonConfig, HaJsonConfig);
        // DEBUG_V (String("HaJsonConfig: ") + HaJsonConfig);
        mqtt.publish(ha_config.c_str(), 0, true, HaJsonConfig.c_str());

        // publishAttributes ();

    }
    else
    {
        mqtt.publish(ha_config.c_str(), 0, true, "");
    }
    // DEBUG_END;
} // publishHA

//-----------------------------------------------------------------------------
void c_InputMQTT::publishState()
{
    // DEBUG_START;

    DynamicJsonDocument root(1024);
    JsonObject JsonConfig = root.createNestedObject("MQTT");

    JsonConfig["state"] = (true == stateOn) ? String(ON) : String(OFF);

    // populate the effect information
    ((c_InputEffectEngine*)(pEffectsEngine))->GetMqttConfig (JsonConfig);

    String JsonConfigString;
    serializeJson(JsonConfig, JsonConfigString);
    // DEBUG_V (String ("JsonConfigString: ") + JsonConfigString);
    // DEBUG_V (String ("topic: ") + topic);

    mqtt.publish(topic.c_str(), 0, true, JsonConfigString.c_str());

    // DEBUG_END;

} // publishState
