#include <Particle.h>
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#include <ArduinoJson.h>
#include <HttpClient.h>
#include <math.h>
#include <Stepper.h>

const float kDewPointBNoUnit   = 17.67;
const float kDewPointCDegC     = 243.5;
const float kMinDisplayTempC   = -4.0;  // record low temp  -8.89 C ( 16 F) as of May 30, 2019
const float kMaxDisplayTempC   = 52.0;  // record high temp 50.00 C (122 F)
const int   kTempDisplaySteps  = 450;   
const float kMinDisplayDewC    = -30.44;// record low dew point -30.55 C (-23 F)
const float kMaxDisplayDewC    = 26.67; // record high dew point 26.11 C ( 79 F)
const int   kDewDisplaySteps   = 360;

int verbosity = 1;            // 0: don't say much, 2: say lots
int makeActualCalls = 1;      // 1 means make real web calls by default, anything else uses a hard-coded JSON doc
int lastHiTempReset = 0;      // weekday, 1=Sunday, 7=Saturday
int hiTempResetHour = 9;      // reset on the hour, this hour every day (0-23)
int pollingInterval = 900000; //milliseconds
unsigned long lastPoll = 0 - pollingInterval; // last time we called the web service

double tempC = -273.15;
double hiTempC = -273.15;
double dewPointC = -273.15;

HttpClient http;
http_header_t headers[] = {
    { "Accept" , "*/*"},
    { "User-agent", "WeatherMojo HttpClient"},
    { NULL, NULL }
};
http_request_t request;
http_response_t response;

StaticJsonDocument<600> doc;


// Set up the gauge motors
Stepper tempStepper(720, D1, D0, D2, D3); // X40 inner ring
int tempStepperPosition = 0;
int tempStepperRightPosition = 0;

Stepper hiStepper(720, D5, D4, D6, D7); // X40 outer ring
int hiStepperPosition = 0;
int hiStepperRightPosition = 0;

Stepper dewStepper(720, A0, A1, A2, A3); // X27
int dewStepperPosition = 0; 
int dewStepperRightPosition = 0;

void setup()
{
    Serial.begin(9600);
    Time.zone(-7);
   
    // set up the httpclient
    request.hostname = "api.openweathermap.org";
    request.port = 80;
    request.path = "/data/2.5/weather?id=5308049&units=metric&APPID=[YOUR OPENWEATHERMAP.ORG APP ID HERE]";

    // register our functions
    registerFunctions();
    
    // set up our steppers
    tempStepper.setSpeed(40);
    hiStepper.setSpeed(40);
    dewStepper.setSpeed(40);
    
    int i;
    for (i = 0; i < 630; i++) 
    {
        tempStepper.step(-1); // enough to be against the counterclockwise stop
        hiStepper.step(-1);
        dewStepper.step(-1);
    }
        
    tempStepper.step(100);  // enough to reach horizontal (0) on each motor
    hiStepper.step(100);
    dewStepper.step(100);
    
    tempStepperPosition = 0;
    hiStepperPosition = 0;
    dewStepperPosition = 0;
                
    //TODO: Take contol of the on-board LED

}


void loop()
{
    unsigned long now = millis();
    
    if ((now - lastPoll) >= (pollingInterval)) 
    {
		lastPoll = now;
        int refreshed = refreshJson();
        if (refreshed == 0)
        {
            // read from the JSON
            // read them into local vars in case we detect an error and want to keep the old ones
            // JSON specified here: https://openweathermap.org/current
            double tC = doc["main"]["temp"];
            int rhC = doc["main"]["humidity"];
            double dpC = tempRhToDewPoint(tC, rhC);

            bool outOfBounds = false;
            // update these boundaries if I ever leave Phoenix :-)
            if (tC < -20 || tC > 60) 
            { 
                (verbosity > 0) && Particle.publish("OOB_Error", "Out of bounds temperature.", PRIVATE); 
                outOfBounds = true;
            }
            if (rhC < 0 || rhC > 100) 
            { 
                (verbosity > 0) && Particle.publish("OOB_Error", "Out of bounds humidity.", PRIVATE);
                outOfBounds = true;
            }
            if (dpC < -65 || dpC > 50) 
            { 
                (verbosity > 0) && Particle.publish("OOB_Error", "Out of bounds dew point.", PRIVATE); 
                outOfBounds = true;
            }
            
            if (!outOfBounds) 
            {
                tempC = tC;
                dewPointC = dpC;

                // set the temperature needle position
                int newPosition = tempToPosition(tempC);
                tempStepper.step(newPosition - tempStepperPosition);
                tempStepperPosition = newPosition;
                // set the hi temp position, reset it to absolute zero at hiTempResetHour daily
                int nowWeekday = Time.weekday();
                if (lastHiTempReset != nowWeekday && Time.hour() >= hiTempResetHour)
                {
                    hiTempC = -273.15;
                    lastHiTempReset = nowWeekday;
                    (verbosity > 0) && Particle.publish("Hi_Temp_Reset", "Reset daily hi temp", PRIVATE);
                }
                hiTempC = max(hiTempC, tempC);
                newPosition = tempToPosition(hiTempC);
                hiStepper.step(newPosition - hiStepperPosition);
                hiStepperPosition = newPosition;
                
                // set the dewpoint needle position
                newPosition = dewToPosition(dewPointC);
                dewStepper.step(newPosition - dewStepperPosition);
                dewStepperPosition = newPosition;
                
                char strLog[50] = "";
                sprintf(strLog, "T: %3.2f (hi %3.2f) C\nDP: %3.2f C", tempC, hiTempC, dewPointC);
                (verbosity > 0) && Particle.publish("Update", strLog, PRIVATE);
            }
            else if (verbosity > 0)
            {
                char strLog[45] = "";
                sprintf(strLog, "UNCHANGED: Temp: %3.2f C, Dew point %3.2f C", tempC, dewPointC);
                Particle.publish("Update", strLog, PRIVATE);
            }
        }
        else
        {
            Particle.publish("Update_JSON_Error", "Values not updated: Couldn't refresh the JSON doc (response.status != 200)", PRIVATE);
        }
    } // end if polling interval

}

int tempToPosition(double tempDegC)
{
    int returnPos = findPosition(tempDegC, kMinDisplayTempC, kMaxDisplayTempC, kTempDisplaySteps);

    char strLog[45] = "";
    sprintf(strLog, "Temp: %3.2f C is step position %d", tempDegC, returnPos);
    Serial.printlnf(strLog);
    return returnPos;
}

int dewToPosition(double dewPointC)
{
    int returnPos = findPosition(dewPointC, kMinDisplayDewC, kMaxDisplayDewC, kDewDisplaySteps);

    char strLog[45] = "";
    sprintf(strLog, "Dew point: %3.2f C is step position %d", dewPointC, returnPos);
    Serial.printlnf(strLog);
    return returnPos;
}

int findPosition(double degC, float scaleMin, float scaleMax, int scaleSteps)
{
    int returnPos = 0;
    float newPosition = ((degC - scaleMin) / (scaleMax - scaleMin)) * scaleSteps;
    
    newPosition = newPosition + 0.5 - (newPosition<0);
    returnPos = (int)newPosition;
    if (returnPos < 0) returnPos = 0;
    if (returnPos > scaleSteps) returnPos = scaleSteps;
    
    return returnPos;
}

double tempRhToDewPoint(double tempDegC, int relHumid) 
{
    // Magnus equation from https://en.wikipedia.org/wiki/Dew_point
    double dewPointDegC = (kDewPointCDegC * ((log(relHumid/100.0)) + (kDewPointBNoUnit * tempDegC)/(kDewPointCDegC + tempDegC)))/(kDewPointBNoUnit - ((log(relHumid/100.0)) + (kDewPointBNoUnit * tempDegC)/(kDewPointCDegC + tempDegC)));
    return dewPointDegC;
}

int refreshJson() 
{
    // use the global HttpClient http 
    // to make a request to OpenWeatherMap
    // and populate the global StaticJsonDoc doc
    if (makeActualCalls == 1)
    {
        http.get(request, response, headers);
        int responseStatus = response.status;

        if (responseStatus == 200)
        {
            Serial.println(Time.local());
            if (verbosity >=2 ) Particle.publish("Update", response.body, PRIVATE);
            Serial.println(response.body);
            
            char json[600];
            response.body.toCharArray(json, 600);
            
            DeserializationError error = deserializeJson(doc, json);
            if (error) 
            {
                
                Particle.publish("JSON_Error", error.c_str(), PRIVATE);
                return -1;
            }
        }
        else
        {
            Particle.publish("HTTP_Error", "There was some kind of a problem", PRIVATE);
            Particle.publish("HTTP_Error", response.body, PRIVATE);
            return -1;
        }
        
    }
    else 
    {
        (verbosity > 0) && Particle.publish("Fake_Request", request.hostname + ":" + request.port + request.path, PRIVATE);
        deserializeJson(doc, "{\"weather\":[{\"id\":801,\"main\":\"Clouds\",\"description\":\"few clouds\",\"icon\":\"02d\"}],\"main\":{\"temp\":-3.9,\"pressure\":1005,\"humidity\":16,\"temp_min\":73.99,\"temp_max\":82},\"visibility\":16093,\"wind\":{\"speed\":18.34,\"deg\":210,\"gust\":11.3},\"clouds\":{\"all\":20},\"dt\":1558307423,\"id\":5308049,\"name\":\"Paradise Valley\",\"cod\":200}");

    }
    return 0;
}

void registerFunctions()
{
    // verbosity 0, don't publish
    // verbosity 1, publish only failures
    // verbosity 2, publish successes and failures
    Particle.variable("TempC", tempC);
    Particle.variable("hiTempC", hiTempC);
    Particle.variable("dewPointC", dewPointC);
    bool activeSuccess    = Particle.function("active_mojo",    makeRealRequests);
    bool stepSuccess      = Particle.function("step_mojo",      takeSteps);
    bool stepspeedSuccess = Particle.function("stepspeed_mojo", newSpeed);
    bool pollingSuccess   = Particle.function("polling_mojo",   setPollingInterval);
    bool verboseSuccess   = Particle.function("verbose_mojo",   setVerbosity);
    
    if (verbosity > 0)
    {
        if (!activeSuccess)    { Particle.publish("Function_Error", "active_mojo function registration failed",    PRIVATE); delay(1000); }
        if (!stepSuccess)      { Particle.publish("Function_Error", "step_mojo function registration failed",      PRIVATE); delay(1000); }
        if (!stepspeedSuccess) { Particle.publish("Function_Error", "stepspeed_mojo function registration failed", PRIVATE); delay(1000); }
        if (!pollingSuccess)   { Particle.publish("Function_Error", "polling_mojo function registration failed",   PRIVATE); delay(1000); }
        if (!verboseSuccess)   { Particle.publish("Function_Error", "verbose_mojo function registration failed",   PRIVATE); }
        if (verbosity >= 2)
        {
            if (activeSuccess && stepSuccess && stepspeedSuccess && pollingSuccess && verboseSuccess)
                Particle.publish("Function_Success", "all functions registered successfully", PRIVATE);
            else if (activeSuccess || stepSuccess || stepspeedSuccess || pollingSuccess || verboseSuccess)
                Particle.publish("Function_Partial_Success", "other functions registered successfully", PRIVATE);
        }
    }
}

// Cloud function to switch web requests on/off
int makeRealRequests(String command) 
{
    // if command == "yes", then make real web requests for weather
    if (command == "yes") 
    { 
        (verbosity >= 1) && Particle.publish("Command", "Make real requests", PRIVATE);
        makeActualCalls = 1; 
    }
    else // anything else, make fake requests
    {
        (verbosity >= 1) && Particle.publish("Command", "DO NOT make real requests", PRIVATE);
        makeActualCalls = 0; 
    }
    
    return 0;
}

int takeSteps(String command)
{
    //TODO: add error handling
    int steps = command.toInt();
    tempStepper.step(steps);
    if (verbosity >= 1)
    {
        char strLog[25] = "";
        sprintf(strLog, "Moved motor %3.1f steps", steps);
        Particle.publish("Command", strLog, PRIVATE);
    }
    return 0;
}

int newSpeed(String command)
{
    //TODO: add error handling
    int newRpm = command.toInt();
    tempStepper.setSpeed(newRpm);
    hiStepper.setSpeed(newRpm);
    dewStepper.setSpeed(newRpm);
    if (verbosity >= 1)
    {
        char strLog[25] = "";
        sprintf(strLog, "Set speed to %d rpm", newRpm);
        Particle.publish("Command", strLog, PRIVATE);
    }
    return 0;
}

int setPollingInterval(String command)
{
    int newMs = command.toInt();
    if (newMs < 5000) newMs = 900000;
    pollingInterval = newMs;
    if (verbosity >= 1)
    {
        char strLog[25] = "";
        sprintf(strLog, "Set polling interval to %d ms", newMs);
        Particle.publish("Command", strLog, PRIVATE);
    }
    return 0;
}

int setVerbosity(String command)
{
    int newVerbosity = command.toInt();
    if (newVerbosity < 0) newVerbosity = 0;
    if (newVerbosity > 2) newVerbosity = 2;
    verbosity = newVerbosity;
    if (verbosity >= 1)
    {
        char strLog[25] = "";
        sprintf(strLog, "Set verbosity to %d", verbosity);
        Particle.publish("Command", strLog, PRIVATE);
    }
   return 0;
}



//TODO: finish the single-step-per-loop implementation
//TODO: replace observed hi temp with forecast hi temp
//http://api.weatherbit.io/v2.0/forecast/daily?key=5b822cc6043144ed98612881846d5d51&days=1&city_id=5308049&units=M
//http://api.weatherbit.io/v2.0/current?key=5b822cc6043144ed98612881846d5d51&city_id=5308049&units=M
//TODO: replace DewPoint calculation with lookup in the JSON as long as we're switching
//Current weather conditions:  https://api.weather.gov/stations/KSDL/observations
//    doc["features"]["properties"]["temperature"]["value"] == temp
//    doc["features"]["properties"]["dewpoint"]["value"] == dewpoint
//Forecast weather conditions: https://api.weather.gov/gridpoints/PSR/160,62/forecast 
//    doc["properties"]["period"][0]["temperature"] == forecast hi temperature
//    If jsonpath is there, $.properties.periods[?(@.name=='This Afternoon')].temperature should get us the hi temp
//    Otherwise, maybe max(["properties"]["period"][0].temperature, ["properties"]["period"][1].temperature) 
//       would get us the higher of tonight's overnight low and today's high or tonight's overnight low and tomorrow's high?
