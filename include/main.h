#ifndef __MAIN_HPP__
#define __MAIN_HPP__

#include <Arduino.h>

#include "esp_camera.h"
#include <WiFi.h>
#include <SPIFFS.h>

void saveEvent(String type, int value);

void startCameraServer();
String getGlobalIpAddress();

void beforeCameraInit();
void initCamera();
void afterCameraInit();
void onStartup();

void setPOSHOLE(int poshole);
void setPOSCONT(int poscont);
void servoTo(int pos);
void feedNow(int amount);
void setFeedTimes(String stimes);
String getFeedTimes();
void setFoodAmount(int count);
int getFoodAmount();
void setFlash(bool);
int getFlash();
String getFeedLog();
void clearEvents();
void setTopLed(bool, bool);
int getTopLed();
void setFlashBrightness(int brightness);
void setTopLedBrightness(int brightness);
int getFlashBrightness();
int getTopLedBrightness();
void setTopLedOnTime(int time);
void setTopLedOnDuration(int time);
int getTopLedOnTime();
int getTopLedOnDuration();
void feederJob();
void sendMail(String ip);

#endif // __MAIN_HPP__