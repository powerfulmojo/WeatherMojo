#include <Particle.h>
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#include <ArduinoJson.h>
#include <PowerShield.h>
#include "ePaperWeather.h"
#include "epd.h"

int wake_epaper = D2;       // epaper wakeup pin
int pwr_connected = D4;     // connected to power shield "USB Good" (INPUT_PULLUP)

double Temp = -199;
double HiTemp = -199;
double DewPoint = -199;

PowerShield batteryMonitor;
LEDSystemTheme theme;       // Custom LED theme, set in setup()
int cityId = 5308655;       // city ID to get weather for (https://www.weatherbit.io/api/meta) (5308655 for Phoenix, 5308049 for PV)
int tZone  = -7;            // Time zone
int verbosity = 2;          // 0: don't say much, 2: say lots

String LastUpdateTimeString = "";
float LoBatThreshold = 15;
float HiBatThreshold = 95;
float PermanentShutdown = 10;



void setup() {
    Serial.begin(9600);
    Time.zone(tZone);
    
    // get the battery meter ready to work
    batteryMonitor.begin();     
    batteryMonitor.quickStart();
    pinMode(pwr_connected, INPUT_PULLUP);

    // get the epaperdisplay ready to work
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
            char strLog[50] = "";
            sprintf(strLog, "T: %3.2f (hi %3.2f) F\nDP: %3.2f F", Temp, HiTemp, DewPoint);
            if (verbosity > 0) Particle.publish("Updated", strLog, PRIVATE);
            LastUpdateTimeString = Time.format(Time.now(),TIME_FORMAT_ISO8601_FULL);
            
            float bat_percent = batteryMonitor.getSoC();
            bool show_lo_bat = (bat_percent < LoBatThreshold);
            bool show_hi_bat = (bat_percent > HiBatThreshold && digitalRead(pwr_connected) == LOW);
            if (bat_percent < PermanentShutdown) System.sleep(SLEEP_MODE_DEEP); // sleep forever
            
            epd_wakeup();
            ePaperWeather epw = ePaperWeather(Temp, HiTemp, DewPoint, show_lo_bat, show_hi_bat);
            
            //TODO: implement a proper wait for next time and sleep schedule
            
            delay(60000);
        }
    }
    else
    {
        // wifi wasn't ready, give it a sec
        delay(1000);
    }
}


// Publish a weathermojo_request event
// The response will be hanlded elsewhere
// return true if it seems like we got a response
// return false if we timed out waiting for one
bool request_cheez(void)
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

// Turn on our serial pin and define a wake-up pin
void epd_init(void)
{
	Serial1.begin(115200);
	pinMode(wake_epaper, OUTPUT);
}

// Wake the ePaper module with a pulse on the wake-up pin
void epd_wakeup(void)
{
	digitalWrite(wake_epaper, LOW);
	delayMicroseconds(10);
	digitalWrite(wake_epaper, HIGH);
	delayMicroseconds(500);
	digitalWrite(wake_epaper, LOW);
	delay(10);
}

