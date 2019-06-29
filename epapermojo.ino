#include <Particle.h>
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#include <ArduinoJson.h>
#include "HttpClient.h"
#include "WeatherbitKey.h"
#include "epd.h" // Defines pin D2 as wake_up
// Include your own weatherbit API key in WeatherbitKey.h or uncomment & enter it on the next line
// char[33] apiKey = "0123456789abcdef0123456789abcdef";
//#include "bigfont.h" // drawing font bitmaps longhand is a pain



int   cityId = 5308655; // city ID to get weather for (https://www.weatherbit.io/api/meta) (5308655 for Phoenix, 5308049 for PV)
int   tZone  = -7;      // Time zone

LEDSystemTheme theme;         // Custom LED theme, set in setup()
int verbosity = 2;            // 0: don't say much, 2: say lots
int makeActualCalls = 1;      // 1 means make real web calls. Any other value stops making web calls.
int inServiceMode = 0;        // 1 means we're in service mode, don't sleep
unsigned int pollingInterval = 900; //Seconds
unsigned long lastPoll = 0 - pollingInterval; // last time we called the web service
String lastUpdateTimeString = "";

double tempF = -459.67;
double hiTempF = -459.67;
double dewPointF = -459.67;

// wake-up pin
//epaper device wake-up pin set in epd.cpp int wake_up = D2;
int wake_up = D2; // e-paper wakeup
int wake_particle_button = D4;
int service_mode = D6;

//global httpclient stuff
HttpClient http;
http_header_t headers[] = {
    { "Accept" , "application/json"},
    { "User-agent", "powerfulmojo.com HttpClient"},
    { NULL, NULL }
};
http_request_t request;
http_response_t response;
DynamicJsonDocument weatherDoc(1600);

const int bigWidths[] = {157, 92, 150, 141, 157, 151, 150, 157, 141, 150};
const int lilWidths[] = {87, 53, 77, 74, 82, 80, 80, 82, 74, 79};

void setup()
{
    Serial.begin(9600);
    Time.zone(tZone);
    
    pinMode(wake_particle_button, INPUT_PULLUP);
    pinMode(service_mode, INPUT_PULLUP);
    attachInterrupt(service_mode, enterServiceMode, FALLING);
    
    request.hostname = "api.weatherbit.io";
    request.port = 80;

    // turn off the breathing cyan LED
    theme.setColor(LED_SIGNAL_CLOUD_CONNECTED, 0x00000000); 
    theme.apply(); 
    
    epd_init();
    epd_wakeup();
    epd_set_memory(MEM_TF);     // flash memory (MEM_NAND is onboard, MEM_TF is SD Card)
	epd_screen_rotation(3);     // sideways
	epd_set_color(BLACK, WHITE);// black on white
	epd_clear();
	
    epd_set_en_font(ASCII48);   // med typeface

    epd_disp_bitmap("BACK.BMP", 0, 0);

    registerFunctions();
}



void loop()
{
    Particle.process();
    unsigned long now = millis();
    bool wifiReady = WiFi.ready();

    if (inServiceMode == 1)
    {
        // turn ON the breathing cyan LED
        theme.setColor(LED_SIGNAL_CLOUD_CONNECTED, RGB_COLOR_CYAN); 
        theme.apply();
    }
    else // turn LED off
    {
        theme.setColor(LED_SIGNAL_CLOUD_CONNECTED, 0x00000000);
        theme.apply();
    }

    if (wifiReady) 
    {
		lastPoll = now;
		
		//wake up the e-paper
		epd_wakeup();
        
        Serial.printlnf("Starting a new loop.");
        // once a day, refresh the high temperature data document & get forecast high
        int successfulReset = resetHiTemp();
        if (successfulReset != 0)  hiTempF = -100; // set it to a ridiculous value so we know something is wrong.

        
        // refresh the current conditions data document & get temp + dew point
        int successfulConditionsSet = resetTempAndDewPoint();
        if (successfulConditionsSet == 0)
        {
            char strLog[50] = "";
            sprintf(strLog, "T: %3.2f (hi %3.2f) F\nDP: %3.2f F", tempF, hiTempF, dewPointF);
            Serial.printlnf(strLog);
            (verbosity > 0) && Particle.publish("Updated", strLog, PRIVATE);
            lastUpdateTimeString = Time.format(Time.now(),TIME_FORMAT_ISO8601_FULL);
        }
        else
        {
            Serial.println("at least one of temp and dewpoint was not set properly and I am sad.");
        }
        
        displayTemp(tempF, 0);
        displayTemp(dewPointF, 1);
        displayTemp(hiTempF, 2);

        epd_update();       
        epd_enter_stopmode();
        
        // THIS WILL NEVER LOOP, so leave some time to receive commands or press the service mode button
        delay(5000);
        
        if (inServiceMode == 0)
        {
            System.sleep(SLEEP_MODE_DEEP, pollingInterval);
        }
        else
        {
            // we're in service mode; 
            // still wait out the polling interval, 
            // but do it without sleeping
            int i;
            for (i = 0; i < pollingInterval; i++)
            {
                theme.setColor(LED_SIGNAL_CLOUD_CONNECTED, RGB_COLOR_CYAN);
                theme.apply();
                delay(1000);
             }
            
        }
        
        Serial.println("Can't reach this line, sleeping.");
        
    } // end if wifi ready
    
    delay(1000); // wifi must not have been ready. Wait a sec.
}


void displayTemp(double temp, int type)
{
    // type 0 = main temp
    // type 1 = dew point (left side)
    // type 2 = forecast high (right side)
    
    // where to draw each thing
    int xs[] = {100, 35, 350}; // left edges
    int ys[] = {138, 563, 563};// top edges
    int rs[] = {500, 290, 600};// right edges
    char prefix = (type == 0) ? 'B' : 'S';
    
    // blank out the old values
    epd_set_color(WHITE, BLACK); // white rectangles to erase old data
    epd_fill_rect(xs[type] - 35, ys[type], rs[type], (ys[type] + ((type == 0) ? 217 : 110)));
    epd_set_color(BLACK, WHITE); // back to black on white
	
    temp = temp + 0.5 - (temp < 0);
    int t = (int)temp;
    int x = xs[type];
    int y = ys[type];
    
    int huns = (int)(abs(t) >= 100);
    int tens = (int)(abs(t - (huns * 100)) / 10);
    int ones = (int)(abs(t) % 10);

    char filename[7] = "";
    
    // is it negative? draw a 20x15 rectangle to the left
    // Only dew point can be negative in my neighborhood :-)
    if (type == 1 && t < 0) epd_fill_rect(x + 20, (y + ((type == 0) ? 108 : 55)), (x + 40), (y + ((type == 0) ? 125 : 72)));
    
    sprintf(filename, "%c1.BMP", prefix);
    Serial.println(filename);
    if (huns > 0) epd_disp_bitmap(filename, x, y);
    x = x + ((type == 0) ? bigWidths[1] : lilWidths[1]);
    
    sprintf(filename, "%c%d.BMP", prefix, tens);
    Serial.println(filename);
    if (tens > 0 || huns > 0) epd_disp_bitmap(filename, x, y);
    x = x + ((type == 0) ? bigWidths[tens] : lilWidths[tens]);
    
    sprintf(filename, "%c%d.BMP", prefix, ones);
    Serial.println(filename);
    epd_disp_bitmap(filename, x, y);
}


int resetTempAndDewPoint()
{
    // refresh the json doc with current conditions
    // pull out the dew point and temp
    // if they look reasonable-ish, put them in the global vars
    // if anything goes wrong, return -1
    int returnVal = 0;

    char conditionsRequestPath[100];
    sprintf(conditionsRequestPath, "/v2.0/current?key=%s&city_id=%d&units=I", apiKey, cityId);

    int conditionsRefreshed = refreshJson(conditionsRequestPath);
    if (conditionsRefreshed == 0) 
    {
        double tF = weatherDoc["data"][0]["temp"];
        double dF = weatherDoc["data"][0]["dewpt"];
        Serial.printlnf("conditions retrieved: Temp %f F, DP %f F", tF, dF);
        
        if (tF > 10 && tF < 130) 
        {
            tempF = tF;
        }
        else
        {
            returnVal = -1;
            Serial.printlnf("Temp not updated because %f is out of bounds", tF);
        }
        
        if (dF > -30 && dF < 90) 
        {
            dewPointF = dF;
        }
        else
        {
            returnVal = -1;
            Serial.printlnf("Dew point not updated because %f is out of bounds", dF);
        }
    }
    else
    {
        returnVal = -1;
    }
    
    return returnVal;
}

int resetHiTemp()
{
    // put a forecast in the json document 
    // if it's in a reasonable range, set the hiTempC value
    int returnVal = 0;
    
    Serial.printlnf("resetting hi temp");
    char forecastRequestPath[100];
    sprintf(forecastRequestPath, "/v2.0/forecast/daily?key=%s&days=1&city_id=%d&units=I", apiKey, cityId);
    
    int forecastRefreshed = refreshJson(forecastRequestPath);
    
    if (forecastRefreshed == 0) 
    {
        Serial.println("forecast refresh successful.");
    
        double htF = -100; // if this all fails, display min temp on the scale
        htF = weatherDoc["data"][0]["max_temp"];
        Serial.printlnf("hi temp is now %f", htF);
        if (htF > 10 && htF < 130) 
        {
            hiTempF = htF;
        }
        else 
        {
            Serial.println("out of bounds hi temp"); 
            returnVal = -1;
        }
    }
    else
    {
        Serial.println("forecast refresh failed");
        returnVal = -1;
    }
    return returnVal;
}



int refreshJson(String requestPath)
{
    // use the global httpclient to request a path
    int returnVal = 0;

    if (makeActualCalls == 1)
    {
        request.path = requestPath;
        http.get(request, response, headers);
        int responseStatus = response.status;
        
        if (responseStatus == 200)
        {
            // great
            Serial.printlnf("Got a response of %d bytes", response.body.length());
            Serial.println(response.body);
            
            char json[1600];
            response.body.toCharArray(json, 1600);
            DeserializationError error = deserializeJson(weatherDoc, json);
            
            if (error) 
            {
                Serial.printlnf(error.c_str());
                returnVal = -1;
            }
        }
        else
        {
            Serial.printlnf("Error Response %d", response.status);
            Serial.printlnf(response.body);
            returnVal = -1;
        }
    }
    else
    {
        Serial.printlnf("not making real calls. LOL nothing matters.");
        returnVal = -1;
    }
    Serial.printf("returning %d\n", returnVal);
    return returnVal;
}

void enterServiceMode()
{
    if (inServiceMode == 0)
    {
        inServiceMode = 1;
        Serial.println("Entering service mode.");
    }
}

void registerFunctions()
{
    Particle.variable("Temp", tempF);
    Particle.variable("DewPoint", dewPointF);
    Particle.variable("HiTemp", hiTempF);
    Particle.variable("MakeActualCalls", makeActualCalls);
    Particle.variable("LastUpdate", lastUpdateTimeString);
    Particle.variable("Service", inServiceMode);
    
    bool success = false;
    success = Particle.function("set_polling_interval", setPollingInterval);
    success ? Serial.println("Registered set_polling_interval") : Serial.println("Failed to register polling_mojo");
    success = Particle.function("set_city_code", setCityCode);
    success ? Serial.println("Registered set_city_code") : Serial.println("Failed to register set_city_code");
    success = Particle.function("set_api_key", setApiKey);
    success ? Serial.println("Registered set_api_key") : Serial.println("Failed to register set_api_key");
    success = Particle.function("set_temp", setTemp);
    success ? Serial.println("Registered set_temp") : Serial.println("Failed to register set_temp");
    success = Particle.function("set_hi_temp", setHiTemp);
    success ? Serial.println("Registered set_hi_temp") : Serial.println("Failed to register set_hi_temp");
    success = Particle.function("set_dew_point", setDewPoint);
    success ? Serial.println("Registered set_dew_point") : Serial.println("Failed to register set_dew_point");
}

int setTemp(String command)
{
    int newTemp = command.toInt();
    tempF = newTemp;
    displayTemp(tempF, 0);
    epd_update();
    return 0;
}

int setHiTemp(String command)
{
    int newTemp = command.toInt();
    hiTempF = newTemp;
    displayTemp(hiTempF, 2);
    epd_update();
    return 0;
}

int setDewPoint(String command)
{
    int newTemp = command.toInt();
    dewPointF = newTemp;
    displayTemp(dewPointF, 1);
    epd_update();
    return 0;
}

int setPollingInterval(String command)
{
    int newSecs = command.toInt();
    pollingInterval = newSecs;
    if (verbosity >= 1)
    {
        char strLog[45] = "";
        sprintf(strLog, "Set polling interval to %d secs", newSecs);
        Particle.publish("Command", strLog, PRIVATE);
    }
    return 0;
}

int setCityCode(String command)
{
    // set the city code we'll get weather for
    int newCode = command.toInt();
    cityId = newCode;
    // and update conditions right away.
    lastPoll = lastPoll - pollingInterval;
    if (verbosity >= 1)
    {
        char strLog[25] = "";
        sprintf(strLog, "Set city code to %d", cityId);
        Particle.publish("Command", strLog, PRIVATE);
    }
    return 0;
}

int setApiKey(String command)
{
    // careful... easy to break everything
    command.toCharArray(apiKey,33);
    if (verbosity >= 1)
    {
        char strLog[25] = "";
        sprintf(strLog, "Set API key to %s", apiKey);
        Particle.publish("Command", strLog, PRIVATE);
    }
    return 0;
}

void epd_init(void)
{
	Serial1.begin(115200);
	pinMode(wake_up, OUTPUT);
}

void epd_wakeup(void)
{
	digitalWrite(wake_up, LOW);
	delayMicroseconds(10);
	digitalWrite(wake_up, HIGH);
	delayMicroseconds(500);
	digitalWrite(wake_up, LOW);
	delay(10);
}
