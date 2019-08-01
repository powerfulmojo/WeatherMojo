#include <Particle.h>
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#include <ArduinoJson.h>
#include "HttpClient.h"
#include "epd.h" // from https://www.waveshare.com/wiki/4.3inch_e-Paper_UART_Module#Resources
// Include your own weatherbit API key in WeatherbitKey.h or uncomment & enter it on the next line
// char[33] apiKey = "0123456789abcdef0123456789abcdef";
#include "WeatherbitKey.h"

// should we call the web for updates, or use pub/sub with another station?
enum { CALL_WEATHERBIT, CALL_WEATHERMOJO };
int weatherSource = CALL_WEATHERBIT;      

int   cityId = 5308655; // city ID to get weather for (https://www.weatherbit.io/api/meta) (5308655 for Phoenix, 5308049 for PV)
int   tZone  = -7;      // Time zone

LEDSystemTheme theme;         // Custom LED theme, set in setup()
int verbosity = 2;            // 0: don't say much, 2: say lots

int inServiceMode = 0;        // 1 means we're in service mode, don't sleep
unsigned int pollingInterval = 900; //Seconds (if you're on the free plan, you're limited to 500 calls/day)
int longSleepHour = 23;       // the first update on or after this hour will result in a long sleep
int longSleepInterval = 18000;// how long to sleep for a long sleep (seconds)
String lastUpdateTimeString = "";

double tempF = -459.67;
double hiTempF = -459.67;
double dewPointF = -459.67;

// wake-up pins & service mode
int wake_epaper = D2;          // epaper wakeup
int wake_particle_button = D4; // momentary switch connects 3.3V to D4 (INPUT_PULLUP)
int service_mode = D6;         // momentary switch connects GND to D6 (INPUT_PULLUP)
int lo_batt = A3;              // read the powerbost's LBO pin (HIGH means battery is fine, LOW means battery is low)

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

// typeface stuff: the width in pixels of each digit 0-9
const int bigWidths[] = {157, 92, 150, 141, 157, 151, 150, 157, 141, 150};
const int lilWidths[] = {87, 53, 77, 74, 82, 80, 80, 82, 74, 79};
enum { UPDATE_TEMP, UPDATE_DEW_POINT, UPDATE_HI_TEMP };

// battery indicator stuff
char loBatBmp[] = "LOBATT.BMP";
int loBatPosition[] = {524, 452}; // height 24px

void setup()
{
    Serial.begin(9600);
    Time.zone(tZone);
    
    pinMode(wake_particle_button, INPUT_PULLUP);
    pinMode(service_mode, INPUT_PULLUP);
    pinMode(lo_batt, INPUT);
    attachInterrupt(service_mode, enterServiceMode, FALLING);
    

    request.hostname = "api.weatherbit.io";
    request.port = 80;

    // turn off the breathing cyan LED
    theme.setColor(LED_SIGNAL_CLOUD_CONNECTED, 0x00000000); 
    theme.apply(); 
    
    epd_init();
    epd_wakeup();
    epd_set_memory(MEM_TF);     // flash memory (MEM_NAND is onboard, MEM_TF is SD Card)
    epd_screen_rotation(3);     // portrait orientation
    epd_set_color(BLACK, WHITE);// black on white
    epd_clear();
    epd_disp_bitmap("BACK.BMP", 0, 0);

    registerFunctions();
}


void loop()
{
    bool wifiReady = WiFi.ready();

    if (wifiReady) 
    {
        
        Serial.printlnf("Starting a new loop.");
        // refresh the high temperature data document & get forecast high
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
        
        epd_wakeup();                             // wake up the e-paper
        displayBattery();                         // display low battery warning if required
        displayTemp(tempF, UPDATE_TEMP);          // Arrange bitmaps for the temperature
        displayTemp(dewPointF, UPDATE_DEW_POINT); // for the dew point
        displayTemp(hiTempF, UPDATE_HI_TEMP);     // for the forecast high temp
        epd_update();                             // tell the screen to show it all
        epd_enter_stopmode();                     // Zzzzzzz
        
        // THIS WILL NEVER LOOP unless we're in service mode
        // so leave some time to receive commands or press the service mode button
        delay(5000);
        
        waitForNextTime(); // either sleeps or delays, depending on whether we're in service mode
        
        Serial.println("I'm in service mode");
        
    } // end if wifi ready
    
    delay(1000); // wifi must not have been ready. Wait a sec.
}

void displayBattery()
{
    // draw a white rectangle to blot out the old state
    epd_set_color(WHITE, BLACK); // white rectangles to erase old data
    epd_fill_rect(loBatPosition[0], loBatPosition[1], loBatPosition[0] + 48, loBatPosition[1] + 24);
    epd_set_color(BLACK, WHITE); // back to black on white
    
    // Uf the LBO pin digitalRead==0, battery voltage has dropped below 3.2V
    // show the low battery graphic
    if (digitalRead(lo_batt) == LOW) epd_disp_bitmap(loBatBmp, loBatPosition[0], loBatPosition[1]);
}

void displayTemp(double temp, int type)
{
    // Display a temp.
    // Temp to display needs to be between -199 and 199
    temp = temp + 0.5 - (temp < 0);
    int t = (int)temp;
    if (t < -199) t = -199;
    if (t >  199) t =  199;
    
    // where to draw each thing
    int ls[] = {0, 0, 304};     // left edges
    int ts[] = {138, 563, 563}; // top edges
    int rs[] = {500, 290, 600}; // right edges
    char prefix = (type == UPDATE_TEMP) ? 'B' : 'S';
 
    // blank out the old value
    epd_set_color(WHITE, BLACK); // white rectangles to erase old data
    epd_fill_rect(ls[type], ts[type], rs[type], (ts[type] + ((type == UPDATE_TEMP) ? 217 : 110)));
    epd_set_color(BLACK, WHITE); // back to black on white
   
    // separate out the digits to be displayed one at a time
    int huns = (int)(abs(t) >= 100);
    int tens = (int)(abs(t - (huns * 100)) / 10);
    int ones = (int)(abs(t) % 10);

    int x = computeLeftEdge((type == UPDATE_DEW_POINT && t < 0), huns, tens, ones, type);
    int y = ts[type];

    char filename[7] = "";
    
    // is it negative? draw a 20x15 rectangle to the left
    // Only dew point can be negative in my neighborhood :-)
    if (type == UPDATE_DEW_POINT && t < 0) 
    {
        epd_fill_rect(x, (y + 55), (x + 20), (y + 72));
        x = x + 30;
    }
    
    if (huns > 0) // display a 1
    {
        sprintf(filename, "%c1.BMP", prefix);
        Serial.println(filename);
        epd_disp_bitmap(filename, x, y);
        x = x + ((type == UPDATE_TEMP) ? bigWidths[1] : lilWidths[1]);
    }
    
    if (tens > 0 || huns > 0) // display a tens digit if the number is 2 or 3 digits long
    {
        sprintf(filename, "%c%d.BMP", prefix, tens);
        Serial.println(filename);
        epd_disp_bitmap(filename, x, y);
        x = x + ((type == UPDATE_TEMP) ? bigWidths[tens] : lilWidths[tens]);
    }
    
    sprintf(filename, "%c%d.BMP", prefix, ones); // always display a ones digit
    Serial.println(filename);
    epd_disp_bitmap(filename, x, y);
}

int computeLeftEdge(bool isNegative, int hundreds, int tens, int ones, int type)
{
    int leftEdge = 0;
    int width = 0;
    
    if (isNegative) { width += 30; Serial.printlnf("Negative, adding 30. Width: %d", width); }
    if (hundreds > 0) { width += (type == UPDATE_TEMP) ? bigWidths[hundreds] : lilWidths[hundreds]; Serial.printlnf("Hundreds width: %d", width); }
    if (tens > 0 || hundreds > 0) { width += (type == UPDATE_TEMP) ? bigWidths[tens] : lilWidths[tens]; Serial.printlnf("Tens width: %d", width); }
    width += (type == UPDATE_TEMP) ? bigWidths[ones] : lilWidths[ones];
    Serial.printlnf("Final width: %d", width );
    
    if (type == UPDATE_TEMP) leftEdge = (int) 300 - (width / 2);
    if (type == UPDATE_DEW_POINT) leftEdge = (int) 150 - (width / 2);
    if (type == UPDATE_HI_TEMP) leftEdge = (int) 450 - (width /2);
    Serial.printlnf("So left edge is %d", leftEdge);
    
    return leftEdge;    
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

    int conditionsRefreshed;
    if (weatherSource == CALL_WEATHERMOJO) 
    {
        // get a JSON from another weather station
        Particle.publish("weathermojo_request", "Need a cheez", PRIVATE);
        String originalUpdateTime = lastUpdateTimeString;
        
        int maxWait = 30;
        int waited = 0;
        conditionsRefreshed = -1;
        while(conditionsRefreshed != 0 && waited < maxWait)
        {
            if (waited > 0) delay(1000);
            waited = waited + 1;
            if (lastUpdateTimeString != originalUpdateTime)
            {
                conditionsRefreshed = 0;
            }
            if (waited == maxWait) Particle.publish("Error", "Never did get a polo outta that marco.", PRIVATE);
        }
    }
    else
    {
        // get JSON from the web
       conditionsRefreshed = refreshJsonFromWeb(conditionsRequestPath);
    }
        
    if (conditionsRefreshed == 0) 
    {
        double tF = weatherDoc["data"][0]["temp"];
        double dF = weatherDoc["data"][0]["dewpt"];
        double hF = -100;
        if (weatherSource == CALL_WEATHERMOJO)
        {
            // if our updates are coming from WeatherMojo, the update has our hi temp too
            hF = weatherDoc["data"][0]["max_temp"];
            if (hF > 10 && hF < 130) hiTempF = hF;
        }
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
    
    int forecastRefreshed = -1;
     if (weatherSource == CALL_WEATHERMOJO) 
    {
        // we're getting updates from the other weather station.
        // the hi temp is already in the doc, and was set by the hnadler.
        // Let's assume that all went fine and we don't need to do anything here
        return returnVal;
    }
    else
    {
        forecastRefreshed = refreshJsonFromWeb(forecastRequestPath);
    }
    
    if (forecastRefreshed == 0) //that's good
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

int refreshJsonFromWeb(String requestPath)
{
    // use the global httpclient to request a path
    int returnVal = 0;

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

    Serial.printf("returning %d\n", returnVal);
    return returnVal;
}


void waitForNextTime()
{
    int interval;
    if (Time.hour() >= longSleepHour) interval = longSleepInterval;
    else interval = pollingInterval;
    
    if (inServiceMode == 0)
    {
        System.sleep(SLEEP_MODE_DEEP, interval);
    }
    else
    {
        // we're in service mode; 
        // still wait out the polling interval, 
        // but do it without sleeping
        theme.setColor(LED_SIGNAL_CLOUD_CONNECTED, RGB_COLOR_CYAN); 
        theme.apply();
        
        int i;
        for (i = 0; i < interval; i++)
        {
           delay(1000);
         }
    }
}

void enterServiceMode()
{
    inServiceMode = 1;
    Serial.println("Entering service mode."); // we'll stay here until the photon is reset manually
}

void registerFunctions()
{
    Particle.variable("Temp", tempF);
    Particle.variable("DewPoint", dewPointF);
    Particle.variable("HiTemp", hiTempF);
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
    
    // subscribe to weather updates, maybe
    if (weatherSource == CALL_WEATHERMOJO) Particle.subscribe("weathermojo_response", cheezHandler, MY_DEVICES);
}

int setTemp(String command)
{
    int newTemp = command.toInt();
    tempF = newTemp;
    epd_wakeup();
    delay(10);
    displayTemp(tempF, UPDATE_TEMP);
    epd_update();
    return 0;
}

int setHiTemp(String command)
{
    int newTemp = command.toInt();
    hiTempF = newTemp;
    epd_wakeup();
    delay(10);
    displayTemp(hiTempF, UPDATE_HI_TEMP);
    epd_update();
    return 0;
}

int setDewPoint(String command)
{
    int newTemp = command.toInt();
    dewPointF = newTemp;
    epd_wakeup();
    delay(10);
    displayTemp(dewPointF, UPDATE_DEW_POINT);
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
	pinMode(wake_epaper, OUTPUT);
}

void epd_wakeup(void)
{
	digitalWrite(wake_epaper, LOW);
	delayMicroseconds(10);
	digitalWrite(wake_epaper, HIGH);
	delayMicroseconds(500);
	digitalWrite(wake_epaper, LOW);
	delay(10);
}

void cheezHandler(const char *event, const char *data)
{
    DeserializationError error = deserializeJson(weatherDoc, data);
    if(!error) lastUpdateTimeString = Time.format(Time.now(),TIME_FORMAT_ISO8601_FULL);
    else Particle.publish("Error", "Problem getting json from the other weather station", PRIVATE);
}

