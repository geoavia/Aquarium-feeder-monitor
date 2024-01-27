// Aquarium feeder monitor project
//
// Author: geoavia@gmail.com

#include "main.h"

#include <ESP32Servo.h>
#include <vector>

const char html_header[] PROGMEM = R"===(
<!DOCTYPE HTML><html><head>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body {
    font-family: Arial, Helvetica, sans-serif;
    font-size: 16px;
}
table {
    font-family: arial, sans-serif;
    border-collapse: collapse;
    width: 100%;
}
td, th {
    border: 1px solid #dddddd;
    text-align: left;
    padding: 8px;
}
tr:nth-child(even) {
    background-color: #dddddd;
}
</style>
</head><body>
<input type="button" value="Clear Log" onclick="window.location.href='/feedlog?clear=1'">
)===";

const char html_footer[] PROGMEM = R"===(</body></html>)===";

bool b_feeder_busy = false;

//Servo servoN1; // used by camera pwm 0
//Servo servoN2; // used by camera pwm 1
Servo feedservo;

//Servo tuning
//http://192.168.100.115:8080/control?pass=glofish&var=servo&val=17

int POS_HOLE = 59; // over the feeding hole position
int POS_CONT = 15; // under the food container position

#define MAX_FEED_COUNT 8

int feedTimes[MAX_FEED_COUNT] = {
	0,
};
int feedCount = 0;
int foodAmount = 0; // 0 - 4
bool flashOn = false;
int flashBrightness = 1; // 1 - 5
bool topledOn = false;
int topledBrightness = 0; // 0 - 100
int topledOnTime = 0;	  // hour (24)
int topledOnDuration = 0; // hour

static int hLastFeed = 0;

String globalIp = "";

static ulong lastSaveSettingsTime = 0L;
static ulong lastFlashTime = 0L;

#define SETTINGS_SAVE_DELAY 5000
#define FLASH_ON_DELAY 60000

const char *EVENTS_FILE_NAME = "/event.log";
const char *SETTINGS_FILE_NAME = "/settings.sav";

#define FLASH_PIN GPIO_NUM_4
#define FLASH_CHANNEL 3 // pwm 3
#define SERVO_PIN GPIO_NUM_13
#define FEED_BUTTON_PIN GPIO_NUM_12
#define DIMMER_ZC_PIN GPIO_NUM_14
#define DIMMER_OUT_PIN GPIO_NUM_15

#define LONGPRESS_MS 3000
#define DOUBLE_CLICK_MS 1000

static ulong lastFBTime = 0L;
static int lastFBState = HIGH;
static uint stepCount = 0;

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600 * 4; // Georgia
const int daylightOffset_sec = 0;	 // Georgia

const int EVENT_LOG_COUNT_MAX = 5;

const char *EVENT_FEED = "Feed";
const char *EVENT_LIGHT = "Light";

static bool dimON = false;
static uint16_t dimPulseTarget = 100;
static uint16_t dimPulseBegin = 100;
static uint16_t pulseWidth = 1;
static volatile uint16_t dimCounter;
static volatile bool zeroCross;

#define DIM_TARGET_DELAY_AUTO 300 // 30 sec
#define DIM_TARGET_DELAY_MANUAL 5 // 500 ms

static bool dimTargetChange = false;
static int dimTargetDelay = DIM_TARGET_DELAY_MANUAL;

struct FeedEvent
{
	int year;
	int month;
	int day;
	int hour;
	int minute;
	String type;
	int value;
};

std::vector<FeedEvent> eventLog;

#define MAX_EVENTLOG_SIZE 256

void IRAM_ATTR ext_tmr()
{
	if (zeroCross)
	{
		dimCounter++;
		if (dimCounter >= dimPulseBegin)
		{
			digitalWrite(DIMMER_OUT_PIN, HIGH);
		}
		if (dimCounter >= (dimPulseBegin + pulseWidth))
		{
			digitalWrite(DIMMER_OUT_PIN, LOW);
			zeroCross = false;
			dimCounter = 0;
		}
	}
}

static void IRAM_ATTR ext_itr(void *arg)
{
	if (dimON)
		zeroCross = true;
}

void updateDimTarget()
{
	if (topledOn)
	{
		if (!dimON)
			dimPulseBegin = 100;
		int p = 101 - topledBrightness;
		// fool proof check
		if (p <= 0)
			p = 1;
		if (p >= 100)
			p = 100;
		dimPulseTarget = p;
		saveEvent(EVENT_LIGHT, topledBrightness);
		dimTargetChange = true;
	}
	else if (dimON)
	{
		dimPulseTarget = 100;
		saveEvent(EVENT_LIGHT, 0);
		dimTargetChange = true;
	}
}

void updateFlash()
{
	int duty = 250 / ((5 - flashBrightness) * 2 + 1);
	ledcWrite(FLASH_CHANNEL, flashOn ? duty : 0);
	if (flashOn)
		lastFlashTime = millis();
}

void settingsChange()
{
	lastSaveSettingsTime = millis();
}

void set_hLastFeed(FeedEvent fe)
{
	if (fe.type.equals(EVENT_FEED))
	{
		hLastFeed = (fe.day - 1) * 24 + fe.hour;
	}
}

int get_hFeedDelta(struct tm timeinfo)
{
	int h = (timeinfo.tm_mday - 1) * 24 + timeinfo.tm_hour;
	int dh = h - hLastFeed;
	if (dh < 0)
		return h;
	return dh;
}

void loadEvents()
{
	eventLog.clear();
	File file = SPIFFS.open(EVENTS_FILE_NAME, "r");
	if (file)
	{
		while (file.available())
		{
			FeedEvent fe;
			fe.year = file.readStringUntil(',').toInt();
			fe.month = file.readStringUntil(',').toInt();
			fe.day = file.readStringUntil(',').toInt();
			fe.hour = file.readStringUntil(',').toInt();
			fe.minute = file.readStringUntil(',').toInt();
			fe.type = file.readStringUntil(',');
			fe.value = file.readStringUntil('\n').toInt();
			Serial.printf("%d.%d.%d.%d:%d,%s,%d\n", fe.year, fe.month, fe.day, fe.hour, fe.minute, fe.type.c_str(), fe.value);
			eventLog.push_back(fe);
			set_hLastFeed(fe);
		}
	}
	file.close();
}

void saveEvent(String type, int value)
{
	FeedEvent fe;
	struct tm timeinfo;

	getLocalTime(&timeinfo);

	fe.year = timeinfo.tm_year + 1900;
	fe.month = timeinfo.tm_mon + 1;
	fe.day = timeinfo.tm_mday;
	fe.hour = timeinfo.tm_hour;
	fe.minute = timeinfo.tm_min;
	fe.type = type;
	fe.value = value;
	if (eventLog.size() >= MAX_EVENTLOG_SIZE) eventLog.erase(eventLog.begin());
	eventLog.push_back(fe);
	File file = SPIFFS.open(EVENTS_FILE_NAME, "a");
	if (file)
	{
		file.printf("%d,%d,%d,%d,%d,%s,%d\n", fe.year, fe.month, fe.day, fe.hour, fe.minute, fe.type.c_str(), fe.value);
		file.close();
	}
	set_hLastFeed(fe);
}

void clearEvents()
{
	eventLog.clear();
	SPIFFS.remove(EVENTS_FILE_NAME);
}

void loadSettings()
{
	File file = SPIFFS.open(SETTINGS_FILE_NAME, "r");
	if (file)
	{
		if (file.available())
		{
			setFeedTimes(file.readStringUntil('\n'));
			foodAmount = file.readStringUntil('\n').toInt();
			flashBrightness = file.readStringUntil('\n').toInt();
			topledBrightness = file.readStringUntil('\n').toInt();
			topledOnTime = file.readStringUntil('\n').toInt();
			topledOnDuration = file.readStringUntil('\n').toInt();
			globalIp = file.readStringUntil('\n');
			globalIp.trim();
			Serial.printf("Feed amount: %d, Times: [", foodAmount);
			for (int i = 0; i < feedCount; i++)
				Serial.printf("%s%d", i ? "," : "", feedTimes[i]);
			Serial.printf("]\nLight ON Time: %d, Duration: %d, Brightness: %d\n", topledOnTime, topledOnDuration, topledBrightness);
			Serial.printf("Global IP: [%s]\n", globalIp.c_str());
		}
	}
	file.close();
}

void saveSettings()
{
	File file = SPIFFS.open(SETTINGS_FILE_NAME, "w");
	if (file)
	{
		Serial.println("Saving Settings");
		file.println(getFeedTimes());
		file.println(foodAmount);
		file.println(flashBrightness);
		file.println(topledBrightness);
		file.println(topledOnTime);
		file.println(topledOnDuration);
		file.println(globalIp);
	}
	file.close();
}

String getFeedLog()
{

	String log = html_header;
	log += "<p>WiFi Signal level: <b>";
	log += WiFi.RSSI();
	log += "</b></p>";
	log += "<table>";
	log += "<tr><th>Date</th><th>Time</th><th>Event</th><th>Value</th></tr>";
	char sline[255];
	for (int i = eventLog.size() - 1; i >= 0; i--)
	{
		sprintf(sline, "<tr><td>%2d.%02d.%d</td><td>%02d:%02d</td><td>%s</td><td>%d</td></tr>\n",
				eventLog[i].day,
				eventLog[i].month,
				eventLog[i].year,
				eventLog[i].hour,
				eventLog[i].minute,
				eventLog[i].type.c_str(),
				eventLog[i].value);
		log += sline;
	}
	log += "</table>";
	log += html_footer;
	return log;
}

void beforeCameraInit()
{
	//servoN1.attach(GPIO_NUM_16);  // used by camera pwm 0
	//servoN2.attach(GPIO_NUM_16);  // used by camera pwm 1
	//feedservo.setPeriodHertz(50); // standard 50 hz servo
	feedservo.attach(SERVO_PIN);  // pwm 2

	ledcSetup(FLASH_CHANNEL, 5000, 8);
	ledcAttachPin(FLASH_PIN, FLASH_CHANNEL); // pwm 3

	pinMode(FEED_BUTTON_PIN, INPUT_PULLUP);

	if (!SPIFFS.begin(true))
	{
		Serial.println("An Error has occurred while mounting SPIFFS");
		return;
	}

	loadSettings();
	loadEvents();

	// initial feeder servo position
	servoTo(POS_CONT);
}

void afterCameraInit()
{
	pinMode(DIMMER_ZC_PIN, INPUT_PULLUP);
	pinMode(DIMMER_OUT_PIN, OUTPUT);

	// ESP_INTR_FLAG_EDGE | ESP_INTR_FLAG_IRAM
	esp_err_t err;
	// err = gpio_install_isr_service(0);
	// if (err != ESP_OK) {
	//     Serial.printf("gpio install failed with error 0x%x \r\n", err);
	// }
	err = gpio_isr_handler_add(DIMMER_ZC_PIN, &ext_itr, (void *)14);
	if (err != ESP_OK)
	{
		Serial.printf("handler add failed with error 0x%x \r\n", err);
	}
	err = gpio_set_intr_type(DIMMER_ZC_PIN, GPIO_INTR_POSEDGE);
	if (err != ESP_OK)
	{
		Serial.printf("set intr type failed with error 0x%x \r\n", err);
	}

	hw_timer_t *tmr = timerBegin(3, 250, true);
	timerAttachInterrupt(tmr, ext_tmr, true);
	timerAlarmWrite(tmr, 30, true);
	timerAlarmEnable(tmr);
}

void servoTo(int pos)
{
	feedservo.write(pos);
}

void shakeIt(uint delta, uint amount = 4)
{
	int pos = feedservo.read();
	for (int i = 0; i < amount; i++)
	{
		servoTo(pos - delta);
		delay(100);
		servoTo(pos + delta);
		delay(100);
	}
	servoTo(pos);
}

void feedNow(int amount)
{
	if (b_feeder_busy)
		return;
	Serial.print("Feeding...");
	b_feeder_busy = true;
	servoTo(POS_CONT);
	for (int i = 0; i < amount; i++)
	{
		shakeIt(3);
		delay(1000);
		servoTo(POS_HOLE);
		shakeIt(3);
		delay(1000);
		servoTo(POS_CONT);
		shakeIt(3);
		delay(1000);
	}
	saveEvent(EVENT_FEED, amount);
	Serial.println("Done");
	b_feeder_busy = false;
}

void setFeedTimes(String stimes)
{
	String str = stimes;
	int end = -1;
	int beg = 0;
	feedCount = 0;
	while ((end = str.indexOf(",")) != -1)
	{
		feedTimes[feedCount] = str.substring(beg, end).toInt();
		beg = end + 1;
		str = str.substring(beg);
		feedCount++;
	}
	feedTimes[feedCount] = str.toInt();
	feedCount++;
	settingsChange();
}

String getFeedTimes()
{
	String str = "";
	for (int i = 0; i < feedCount; i++)
	{
		if (i)
			str += ",";
		str += feedTimes[i];
	}
	return str;
}

void setFoodAmount(int count)
{
	foodAmount = count;
	settingsChange();
}

void setFlash(bool on)
{
	if (on == flashOn)
		return;
	flashOn = on;
	updateFlash();
}

int getFlash()
{
	return flashOn ? 1 : 0;
}

void setFlashBrightness(int brightness)
{
	if (flashBrightness == brightness)
		return;
	flashBrightness = brightness;
	updateFlash();
	settingsChange();
}

void setTopLed(bool on, bool manual)
{
	if (on == topledOn)
		return;
	topledOn = on;
	dimTargetDelay = (manual ? DIM_TARGET_DELAY_MANUAL : DIM_TARGET_DELAY_AUTO);
	updateDimTarget();
}

int getTopLed()
{
	return topledOn ? 1 : 0;
}

void setTopLedBrightness(int brightness)
{
	if (topledBrightness == brightness)
		return;
	topledBrightness = brightness;
	dimTargetDelay = DIM_TARGET_DELAY_MANUAL;
	updateDimTarget();
	settingsChange();
}

void setTopLedOnTime(int time)
{
	topledOnTime = time;
	settingsChange();
}

void setTopLedOnDuration(int time)
{
	topledOnDuration = time;
	settingsChange();
}

int getTopLedOnTime()
{
	return topledOnTime;
}

int getTopLedOnDuration()
{
	return topledOnDuration;
}

int getFlashBrightness()
{
	return flashBrightness;
}

int getTopLedBrightness()
{
	return topledBrightness;
}

int getFoodAmount()
{
	return foodAmount;
}

void feedCheck(struct tm timeinfo)
{
	if (foodAmount > 0)
	{
		for (int i = 0; i < feedCount; i++)
		{
			if (timeinfo.tm_hour == feedTimes[i] && get_hFeedDelta(timeinfo) > 2)
			{
				feedNow(foodAmount);
			}
		}
	}
}

void lightCheck(struct tm timeinfo, bool bootup)
{
	// mktime(&timeinfo);
	if (topledOnTime > 0)
	{
		int off = (topledOnTime + topledOnDuration) % 24; // off time

		if (bootup)
		{
			if (timeinfo.tm_hour >= topledOnTime && timeinfo.tm_hour < off)
			{
				setTopLed(true, true);
			}
			else
			{
				setTopLed(false, true);
			}
		}
		else
		{
			// check for on
			if (timeinfo.tm_hour == topledOnTime && timeinfo.tm_min < 5)
			{
				setTopLed(true, false);
				return;
			}
			// check for off
			if (timeinfo.tm_hour == off && timeinfo.tm_min < 5)
			{
				setTopLed(false, false);
				return;
			}
		}
	}
}

void saveSettingsCheck()
{
	if (lastSaveSettingsTime > 0L && ((millis() - lastSaveSettingsTime) > SETTINGS_SAVE_DELAY))
	{
		lastSaveSettingsTime = 0L;
		saveSettings();
	}
}

void flashCheck()
{
	if (lastFlashTime > 0L && ((millis() - lastFlashTime) > FLASH_ON_DELAY))
	{
		lastFlashTime = 0L;
		setFlash(false);
	}
}

// check for global ip address change on startup
void checkGlobalIpAddress()
{
	String ip = getGlobalIpAddress();
	if (ip.length() > 8 && !ip.equals(globalIp))
	{
		Serial.print("Global IP Address has changed: ");
		Serial.println(ip);
		globalIp = ip;
		settingsChange();
		Serial.println("Sending Email...");
		sendMail(ip);
	}
}

// things to do on startup just before the main loop begins
void onStartup()
{
	struct tm timeinfo;

	configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
	checkGlobalIpAddress();
	delay(1000);
	getLocalTime(&timeinfo);
	// Serial.println(&timeinfo, "%A, %B %d %Y %H:%M");
	lightCheck(timeinfo, true);
}

void feederJob()
{
	stepCount = (stepCount + 1) % 36000; // cycle ~ each hour

	if (dimTargetChange && (stepCount % dimTargetDelay == 0)) // ~ each dimTargetDelay*100 ms
	{
		dimTargetChange = (dimPulseBegin != dimPulseTarget);
		if (dimTargetChange)
		{
			dimPulseBegin += ((dimPulseTarget > dimPulseBegin) ? 1 : -1);
			dimON = (topledOn || dimPulseBegin < 100);
			Serial.print("dimmer: ");
			Serial.print(dimON);
			Serial.print(" - ");
			Serial.println(dimPulseBegin);
		}
	}

	if (stepCount % 50 == 0) // ~ each 5 sec
	{
		// Serial.print("Signal Level: ");
		// Serial.println(WiFi.RSSI());
		saveSettingsCheck();
		flashCheck();
	}

	if (stepCount % 600 == 0) // ~ each minute
	{
		struct tm timeinfo;
		getLocalTime(&timeinfo);

		// Serial.println(&timeinfo, "%A, %B %d %Y %H:%M");
		feedCheck(timeinfo);
		lightCheck(timeinfo, false);
	}

	// if (stepCount == 0) // ~ each hour
	// {
	// }

	// Feed Button state detection
	int state = digitalRead(FEED_BUTTON_PIN);
	if (state == LOW)
	{
		if (lastFBState != state)
		{
			/*if ((millis() - lastFBTime) < DOUBLE_CLICK_MS)
			{
				// double click
				setTopLed(false);
			}
			else
			{
				// single click
				setTopLed(true);
			}*/
			lastFBTime = millis();
			Serial.println("FEED BUTTON");
		}
		else if ((millis() - lastFBTime) > LONGPRESS_MS)
		{
			feedNow(1);
		}
	}
	lastFBState = state;
}
