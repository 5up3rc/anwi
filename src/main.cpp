#include <..\lib\global_vars.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <esp8266httpclient.h>

#include "packet_capture.h"
#include "debug_print.h"
#include "config.h"
#include "alerts.h"
#include "geofence.h"

uint8_t isConfiguredflag = -1;

void hop_channel()
{
    if (sensor_config.protection_config.is_hop_channel_enabled)
    {
        if (set_channel == MAX_CHANNEL)
        {
            set_channel = INIT_CHANNEL;
        }
        else
        {
            set_channel++;
        }
        if(DEBUG_PRINT)
        {
            Serial.print(" CHAN SET TO : ");
            Serial.println(set_channel);
        }
       wifi_set_channel(set_channel);
    }
}

void loop() 
{
    if ( sensor_config.alert_mode == ALERT_NRF )
    {
        radio_update();    
    }
    
    heartbeat();
    
    if(sensor_config.operation_mode == OPERATION_DETECTION_MODE)
    {
        curTime = millis();
        if(curTime - prevTime >= SCAN_FREQ)
        {    
            if(pkt_info.is_deauth_detected)
            {
                if(deauth_pkt_counter >= MAX_DEAUTH_PKT)
                {
                    pkt_info.attack_type = IS_DEAUTH_ATTACK;
                }
                pkt_info.is_deauth_detected = false;
            }
            hop_channel();
        }
        else
        {
            prevTime = curTime;
            deauth_pkt_counter = 0;
        }

        if (pkt_info.attack_type == IS_EVILTWIN_ATTACK ||  pkt_info.attack_type == IS_DEAUTH_ATTACK )
        {
            send_alert();
            pkt_info.attack_type = -1;
        }
    }

    else if(sensor_config.operation_mode == OPERATION_PROTECTION_MODE)
    {
        delay(5000);
        //recalibrate geofence after regular interval
        recalibrate_transmission_power();

        //set values for pkt_info variable
        unsigned char number_client;
        struct station_info *stat_info;
        int i=1;

        number_client= wifi_softap_get_station_num(); // Count of stations which are connected to ESP8266 soft-AP
        stat_info = wifi_softap_get_station_info();
        if(DEBUG_PRINT)
        {
            Serial.print(" Total connected_client are = ");
            Serial.println(number_client);

            if(stat_info == NULL)
            {
                Serial.print("ERROR: Stat_info is null");
            }
        }
        //send alert for each connected client.. Currently no duplicate checked 
        char bssid_mac[18];
        while (stat_info != NULL) 
        {
            pkt_info.attack_type = IS_GEOFENCE_ATTACK;
            WiFi.macAddress(pkt_info.frame_hdr.bssid_address);
            pkt_info.channel = 1;
            pkt_info.rssi = WiFi.RSSI();
            WiFi.macAddress(pkt_info.frame_hdr.destination_address);
            MEMCPY(pkt_info.frame_hdr.source_address,stat_info->bssid,6);
            pkt_info.frame_hdr.deauth.reason_code = 0;
            sprintf(bssid_mac,MACSTR , MAC2STR(pkt_info.frame_hdr.bssid_address));
            Serial.print("Attacker MAC : ");
            Serial.println(bssid_mac);
            send_alert();
            pkt_info.attack_type = -1;
            stat_info = STAILQ_NEXT(stat_info, next);
            i++;
        }
        pkt_info.attack_type = -1;
    }
}

void setup() 
{
    delay(5000);    
    isConfiguredflag = get_configuration_status();
    Serial.begin(115200);
    Serial.println("\nANWI - All New Wireless IDS\n ");
    if ( isConfiguredflag == 0)
    {
        Serial.println("NOT_CONFIGURED");
        while (get_configuration_status() == 0)
        {
            if(Serial)
            {
                config_sensor_json();
            }
        }
        Serial.println("JUST_CONFIGURED");
        delay(2000);
        ESP.restart();
    }
    else
    {
            Serial.println("ALREADY_CONFIGURED");
            get_config_settings();
            print_config();
            delay(5000);
            if(Serial.read() == 'd')
            {
                clear_configuration();
                Serial.println("CONFIGURATION_CLEARED");
                Serial.println("Rebooting Sensor");
                delay(1000);
                ESP.restart();
            }
            Serial.println("Using existing configuration");
    }

    if ( sensor_config.alert_mode == ALERT_NRF )
    { 
        init_radio();
        radio_update();
    }

    curr_channel = 1;
    //get_config_settings();
    if(sensor_config.operation_mode == OPERATION_DETECTION_MODE)
    {
        Serial.println("ANWI Attack Detection Mode Activated..");
        init_sniffing();
    }
    else if (sensor_config.operation_mode == OPERATION_PROTECTION_MODE)
    {
        Serial.println("ANWI Protection Mode Activated..");
        setup_geofence(sensor_config.protect_ap_info.SSID);
    }
}
