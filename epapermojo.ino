/********************************************************************************
 * epapermojo.ino
 * 
 * Display weather information on a WaveShare 4.3" ePaper display
 * Publishes a weathermojo_request event to the Particle cloud. If there's 
 * another station running farecastmojo.ino, that station will publish a
 * weathermojo_response containing weather info.
 * 
 * Uses a Particle Power Shield to manage battery power. 
 * 
 * forecastmojo.ino is at:
 * https://github.com/powerfulmojo/WeatherMojo/blob/master/forecastmojo.ino
 * waveshare docs are at:
 * https://www.waveshare.com/wiki/4.3inch_e-Paper_UART_Module
 * 
 * Particle Connections:
 * D2 : Waveshare wake-up (yellow)
 * D4 : PowerShield USBPG
 * D6 : Service mode switch (momentary, normally open, connects to GND)
 * GND: Waveshare ground (black), service mode switch, particle wake-up switch
 * TX : Waveshare transmit (green)
 * RX : Waveshare receive (white)
 * RST: Reset switch (momentary, normally open, connects to GND)
 * 
 * ******************************************************************************/

#include <Particle.h>
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#include <ArduinoJson.h>
#include <PowerShield.h>
#include "ePaperWeather.h"
#include "epd.h"

int wake_epaper = D2;   // epaper wakeup pin
int pwr_connected = D4; // connected to power shield "USB Good" (INPUT_PULLUP)
int service_mode = D6;  // momentary switch connects GND to D6 (INPUT_PULLUP)

int Tzone  = -7;                // time zone
int Verbosity = 1;              // 0: only errors, 1: updates and errors
int InServiceMode = 0;          // 1 means we're in service mode, don't sleep
int PollingMins[] = {1, 31};// minutes after the hour to update weather, sorted ascending (always update once at start-up)
int LongSleepHour = 23;         // the first update on or after this hour will result in a long sleep
int LongSleepInterval = 21600;  // how long to sleep for a long sleep (seconds)
float LoBatThreshold = 15;      // display a low battery icon at this %
float HiBatThreshold = 95;      // display a full battery icon when USB is connected and battery is this %
float PermanentShutdown = 10;   // if battery drops to this %, don't wake up next time

double Temp = -199;
double HiTemp = -199;
double DewPoint = -199;

PowerShield BatteryMonitor;
LEDSystemTheme Theme;       // Custom LED theme, set in setup()
String LastUpdateTimeString = "";

void setup() {
    Time.zone(Tzone);
    
    // get the battery meter ready to work
    BatteryMonitor.begin();     
    BatteryMonitor.quickStart();
    pinMode(pwr_connected, INPUT_PULLUP);
    pinMode(service_mode, INPUT_PULLUP);
    attachInterrupt(service_mode, enterServiceMode, FALLING);
    
    // turn off the breathing cyan LED
    Theme.setColor(LED_SIGNAL_CLOUD_CONNECTED, 0x00000000); 
    Theme.apply(); 

    // get the epaper display ready to work
	epd_init();                 
    
    // listen for responses to our published weathermojo_request events
    Particle.subscribe("weathermojo_response", receive_cheez, MY_DEVICES);
}



void loop() {
    bool wifiReady = WiFi.ready();

    if (wifiReady) 
    {
        if (request_cheez()) // we did not time out waiting for a response
        {
            // figure out if we need to display battery indicators
            float bat_percent = BatteryMonitor.getSoC();
            bool usb_connected = (digitalRead(pwr_connected) == LOW);
            bool show_lo_bat = (bat_percent < LoBatThreshold);
            bool show_hi_bat = (bat_percent > HiBatThreshold && usb_connected);
            
            // wake up the ePaper and put the temperatures on it
            ePaperWeather epw = ePaperWeather(Temp, HiTemp, DewPoint, show_lo_bat, show_hi_bat);
            if (Verbosity > 0)
            {
                char strLog[250] = "";
                sprintf(strLog, "%3.1f,%3.1f,%3.1f,%3.1f,%s", Temp, HiTemp, DewPoint, bat_percent, (usb_connected ? "true" : "false"));
                 Particle.publish("Update", strLog, PRIVATE);
            }

            if (bat_percent < PermanentShutdown) // sleep forever
            { 
                epw.Clear(); 
                epw.Sleep(); 
                if (Verbosity > 0) Particle.publish("Battery", "Battery was too low, going into deep sleep.", PRIVATE);
                System.sleep(SLEEP_MODE_DEEP); 
            } 
        }
        // THIS WILL NEVER LOOP unless we're in service mode
        // so leave some time to receive commands or press the service mode button
        delay(5000);
        waitForNextTime(); // either sleep or delay, depending on whether we're in service mode
    }
    // wifi wasn't ready, give it a sec
    delay(1000);
}


// Publish a weathermojo_request event
// The response will be hanlded in receive_cheez
// return true if it seems like we got a response
// return false if we timed out waiting for one
bool request_cheez()
{
    bool success = true;
    
    Particle.publish("weathermojo_request", "Need a cheez", PRIVATE);
    String originalUpdateTime = LastUpdateTimeString;
    
    int maxWait = 30;
    int waited = 0;

    while(LastUpdateTimeString == originalUpdateTime && waited < maxWait)
    {
        if (waited > 0) delay(1000);
        waited = waited + 1;
        if (waited >= maxWait) 
        {
            Particle.publish("Error", "No cheez.", PRIVATE);
            success = false;
        }
    }
    return success;
}

// Read the JSON doc out of the weathermojo_response event
// set the global vars Temp, HiTemp, and DewPoint 
// Publish error events if anything goes wrong
void receive_cheez(const char *event, const char *data)
{
    DynamicJsonDocument weatherDoc(1600);
    DeserializationError error = deserializeJson(weatherDoc, data);
    if(!error) LastUpdateTimeString = Time.format(Time.now(),TIME_FORMAT_ISO8601_FULL);
    else Particle.publish("Error", "Problem getting json from the other weather station", PRIVATE);

    double tF = weatherDoc["data"][0]["temp"];
    if (tF > 10 && tF < 130) Temp = tF;
    else Particle.publish("Error", "Temp out of bounds: %f", tF);
    
    double hF = weatherDoc["data"][0]["max_temp"];
    if (hF > 10 && hF < 130) HiTemp = hF;
    else Particle.publish("Error", "High temp out of bounds: %f", hF);
    
    double dF = weatherDoc["data"][0]["dewpt"];
    if (dF > -30 && dF < 90) DewPoint = dF;
    else Particle.publish("Error", "Dew point out of bounds: %f", dF);
}

// if we're in normal mode, go into deep sleep 
// if we're in service mode, don't go into deep sleep, use delay() and stay on the wifi
void waitForNextTime()
{
    int sleepSeconds = secondsToNextTime();
    
    if (InServiceMode == 0)
    {
        System.sleep(SLEEP_MODE_DEEP, sleepSeconds);
    }
    else // we're in service mode. still wait out the polling interval, but do it without sleeping
    {
        Theme.setColor(LED_SIGNAL_CLOUD_CONNECTED, RGB_COLOR_CYAN); 
        Theme.apply();
        for (int i = 0; i < sleepSeconds; i++) delay(1000);
    }
}

// return the number of seconds to sleep so we wake up again at the right time past the hour
// minutes past the hour when we should wake up are in the global PollingMins[]
int secondsToNextTime()
{
    int sleepSeconds = 0;
    int currentMinute = Time.minute(); 
    int updateMinute = PollingMins[0]; 
    int itemCount = (sizeof(PollingMins)/sizeof(*PollingMins));
    for (int i = 1; i < itemCount; i++)
    {
        if (currentMinute < PollingMins[i])
        {
            updateMinute = PollingMins[i];
            break;
        }
    }
    
    if (updateMinute < currentMinute) updateMinute += 60;
    
    if (Time.hour() >= LongSleepHour) sleepSeconds = LongSleepInterval;
    else sleepSeconds = ((updateMinute - currentMinute) * 60) - Time.second();

    if (Verbosity > 0) { 
        char strLog[50] = "";
        sprintf(strLog, "currMin: %d, updateMin: %d, sleepSecs: %d", currentMinute, updateMinute, sleepSeconds);
        Particle.publish("Sleeping", strLog, PRIVATE);
    }

    return sleepSeconds;
}

// set a global var that will tell waitForNextTime() not to go into deep sleep
// useful for keeping the Photon awake long enough to flash new code
void enterServiceMode() 
{ 
    InServiceMode = 1;  
}

// Turn on our serial pin and define a wake-up pin
void epd_init()
{
	Serial1.begin(115200);
	pinMode(wake_epaper, OUTPUT);
}

// Wake the ePaper module with a pulse on the wake-up pin
void epd_wakeup()
{
	digitalWrite(wake_epaper, LOW);
	delayMicroseconds(100);
	digitalWrite(wake_epaper, HIGH);
	delayMicroseconds(200);
	digitalWrite(wake_epaper, LOW);
}

