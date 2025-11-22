//////////////////////////////////////////////
// Author: geoavia@gmail.com

#include "main.h"

#include "soc/soc.h"             // disable brownout problems
#include "soc/rtc_cntl_reg.h"    // disable brownout problems

const char *ssid = "SR-71U";
const char *password = "BlackBird";

IPAddress local_IP(192, 168, 100, 115);
IPAddress gateway(192, 168, 100, 1);
IPAddress subnet(255, 255, 0, 0);
IPAddress primaryDNS(192, 168, 100, 1); // optional
IPAddress secondaryDNS(8, 8, 8, 8); // optional	

void setup()
{
    //Serial.begin(115200);

 	Serial.println("Serial Init");

    Serial.setDebugOutput(false);
    Serial.println();

    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector

    beforeCameraInit();
 	Serial.println("beforeCameraInit");

    initCamera();
 	Serial.println("initCamera");

    afterCameraInit();
 	Serial.println("afterCameraInit");

    // Configures static IP address
    if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) 
    {
        Serial.println("STA Failed to configure");
    }

    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connected");

    startCameraServer();

    Serial.print("Device online at: 'http://");
    Serial.print(WiFi.localIP());
    Serial.println(":8080'");

    onStartup();
}

void loop()
{
    delay(100);
    feederJob();
}
