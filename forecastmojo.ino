#include <Particle.h>
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#include <ArduinoJson.h>
#include "HttpClient.h"
#include <Stepper.h>
#include "WeatherbitKey.h"
// Include your own weatherbit API key in WeatherbitKey.h or uncomment & enter it on the next line
// char[33] apiKey = "0123456789abcdef0123456789abcdef";

// configure the boundaries of our gauges appropriate to Phoenix, AZ, US
const float kMinDisplayTempC   = -6.67; // record low temp  -8.89 C ( 16 F) as of May 30, 2019
const float kMaxDisplayTempC   = 48.89; // record high temp 50.00 C (122 F)
const int   kTempDisplaySteps  = 462;   
const float kMinDisplayDewC    = -26.11;// record low dew point -30.55 C (-23 F)
const float kMaxDisplayDewC    = 23.89; // record high dew point 26.11 C ( 79 F)
const int   kDewDisplaySteps   = 386;

int   cityId = 5308655; // city ID to get weather for (https://www.weatherbit.io/api/meta) (5308655 for Phoenix, 5308049 for PV)
int   tZone  = -7;      // Time zone

LEDSystemTheme theme;         // Custom LED theme, set in setup()
int verbosity = 2;            // 0: don't say much, 2: say lots
int makeActualCalls = 1;      // 1 means make real web calls. Any other value stops making web calls.
unsigned int pollingInterval = 900000; //milliseconds
unsigned long lastPoll = 0 - pollingInterval; // last time we called the web service

double tempC = -273.15;
double hiTempC = -273.15;
double dewPointC = -273.15;

// variables to handle the once-a-day hi temp forecast reset
int lastHiTempReset = 0;      // weekday, 1=Sunday, 7=Saturday
int hiTempResetHour = 4;      // reset on this hour every day (0-23)

//global httpclient stuff
HttpClient http;
http_header_t headers[] = {
    { "Accept" , "* / *"},
    { "User-agent", "powerfulmojo.com HttpClient"},
    { NULL, NULL }
};
http_request_t request;
http_response_t response;
DynamicJsonDocument weatherDoc(1600);

// Set up the gauge motors
Stepper tempStepper(720, D1, D0, D2, D3); // X40 outer ring
int tempStepperPosition = 0;
int tempStepperRightPosition = 0;

Stepper hiStepper(720, D4, D5, D6, D7); // X40 inner spindle
int hiStepperPosition = 0;
int hiStepperRightPosition = 0;

Stepper dewStepper(720, A1, A0, A2, A3); // X27
int dewStepperPosition = 0; 
int dewStepperRightPosition = 0;

void setup()
{
    Serial.begin(9600);
    Time.zone(tZone);
   
    request.hostname = "api.weatherbit.io";
    request.port = 80;

    // set up our steppers
    tempStepper.setSpeed(5);
    hiStepper.setSpeed(5);
    dewStepper.setSpeed(5);
    
    // take negative steps enough to be against the counterclockwise stop
    int i;
    for (i = 0; i < 630; i++) 
    {
        tempStepper.step(-1); 
        hiStepper.step(-1);
        dewStepper.step(-1);
    }
    
    // how far away each motor's counter-clockwise
    // stop is from minimum value on the gauge face
    int tempStepperZero = 20;
    int hiStepperZero = 80;
    int dewStepperZero = 84;
    
    for (i = 0; i <= max(tempStepperZero, max(hiStepperZero, dewStepperZero)); i++)
    {
        if (i < tempStepperZero) tempStepper.step(1); // enough to reach (0) on each scale
        if (i < hiStepperZero) hiStepper.step(1);
        if (i < dewStepperZero) dewStepper.step(1);
    }

    tempStepperPosition = 0; // that makes it official
    hiStepperPosition = 0;   // each motor is pointed
    dewStepperPosition = 0;  // at zero
    
    // turn off the breathing cyan LED
    theme.setColor(LED_SIGNAL_CLOUD_CONNECTED, 0x00000000); 
    theme.apply(); 
    
    registerFunctions();
}



void loop()
{
    unsigned long now = millis();
    
    if ((now - lastPoll) >= (pollingInterval)) 
    {
		lastPoll = now;
        
        Serial.printlnf("Starting a new loop.");
        // once a day, refresh the high temperature data document & get forecast high
        int nowWeekday = Time.weekday();
        if (lastHiTempReset == 0 || (lastHiTempReset != nowWeekday && Time.hour() >= hiTempResetHour))
        {
            lastHiTempReset = nowWeekday;
            int successfulReset = resetHiTemp();
            if (successfulReset != 0)
            {
                // set it to the min temp on the gauge so we know something is wrong.
                hiTempC = kMinDisplayTempC;
                // but try again next time
                lastHiTempReset = 0;
            }
        }
        
        // every polling interval, refresh the current conditions data document & get temp + dew point
        int successfulConditionsSet = resetTempAndDewPoint();
        if (successfulConditionsSet == 0)
        {
            char strLog[50] = "";
            sprintf(strLog, "T: %3.2f (hi %3.2f) C\nDP: %3.2f C", tempC, hiTempC, dewPointC);
            Serial.printlnf(strLog);
            (verbosity > 0) && Particle.publish("Updated", strLog, PRIVATE);
        }
        else
        {
            Serial.println("at least one of temp and dewpoint was not set properly and I am sad.");
        }
        
        // set all motors' correct positions
        tempStepperRightPosition = tempToPosition(tempC);
        hiStepperRightPosition = tempToPosition(hiTempC);
        if (verbosity > 1)
        {
            char strDebugLog[50] = "";
            sprintf(strDebugLog, "Set hi stepper position to %d", hiStepperRightPosition);
            Particle.publish("Debug", strDebugLog, PRIVATE);
        }
        
        dewStepperRightPosition = dewToPosition(dewPointC);

    } // end if polling interval
    
    // every loop we get the motors a little closer to their correct position
    adjustMotors();
}

int resetTempAndDewPoint()
{
    // refresh the json doc with current conditions
    // pull out the dew point and temp
    // if they look reasonable-ish, put them in the global vars
    // if anything goes wrong, return -1
    int returnVal = 0;

    char conditionsRequestPath[100];
    sprintf(conditionsRequestPath, "/v2.0/current?key=%s&city_id=%d&units=M", apiKey, cityId);

    int conditionsRefreshed = refreshJson(conditionsRequestPath);
    if (conditionsRefreshed == 0) 
    {
        double tC = weatherDoc["data"][0]["temp"];
        double dC = weatherDoc["data"][0]["dewpt"];
        Serial.printlnf("conditions retrieved: Temp %f C, DP %f C", tC, dC);
        
        if (tC > -20 && tC < 60) 
        {
            tempC = tC;
        }
        else
        {
            returnVal = -1;
            Serial.printlnf("Temp not updated because %f is out of bounds", tC);
        }
        
        if (dC > -65 && dC < 50) 
        {
            dewPointC = dC;
        }
        else
        {
            returnVal = -1;
            Serial.printlnf("Dew point not updated because %f is out of bounds", dC);
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
    sprintf(forecastRequestPath, "/v2.0/forecast/daily?key=%s&days=1&city_id=%d&units=M", apiKey, cityId);
    
    int forecastRefreshed = refreshJson(forecastRequestPath);
    
    if (forecastRefreshed == 0) 
    {
        Serial.println("forecast refresh successful.");
    
        double htC = kMinDisplayTempC; // if this all fails, display min temp on the scale
        htC = weatherDoc["data"][0]["max_temp"];
        Serial.printlnf("hi temp is now %f", htC);
        if (htC > -10 && htC < 55) 
        {
            hiTempC = htC;
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

int tempToPosition(double tempDegC)
{
    // return the position in positive steps from zero for the given temperature
    int returnPos = findPosition(tempDegC, kMinDisplayTempC, kMaxDisplayTempC, kTempDisplaySteps);

    char strLog[45] = "";
    sprintf(strLog, "Temp: %3.2f C is step position %d", tempDegC, returnPos);
    Serial.printlnf(strLog);
    return returnPos;
}

int dewToPosition(double dewPointC)
{
    // return the position in positive steps from zero for the given temperature
    int returnPos = findPosition(dewPointC, kMinDisplayDewC, kMaxDisplayDewC, kDewDisplaySteps);

    char strLog[45] = "";
    sprintf(strLog, "Dew point: %3.2f C is step position %d", dewPointC, returnPos);
    Serial.printlnf(strLog);
    return returnPos;
}

int findPosition(double degC, float scaleMin, float scaleMax, int scaleSteps)
{
    // given a point in a range, return the position in steps from zero
    int returnPos = 0;
    float newPosition = ((degC - scaleMin) / (scaleMax - scaleMin)) * scaleSteps;
    
    newPosition = newPosition + 0.5 - (newPosition < 0);
    returnPos = (int)newPosition;
    if (returnPos < 0) returnPos = 0;
    if (returnPos > scaleSteps) returnPos = scaleSteps;
    
    return returnPos;
}

void adjustMotors()
{
    // to be called every loop
    // if any motor is a step or more away from its correct position, take one step in the right direction
    // if a motor is already at its min or max position, don't take more steps in that direction
    int moveDirection;
    if (abs(tempStepperRightPosition - tempStepperPosition) >= 1) // if motors are a step or more away from their correct position
    {
        int difference = (tempStepperRightPosition - tempStepperPosition);
        moveDirection = ((difference > 0) - (difference < 0));
        if ((tempStepperPosition + moveDirection) >= 0 && tempStepperPosition + moveDirection <= kTempDisplaySteps)
        {
            tempStepper.step(moveDirection);
            tempStepperPosition = tempStepperPosition + moveDirection;
            if (verbosity > 1)
            {
                char strLog[45] = ""; // in case we need to log anything
                sprintf(strLog, "M=%d, P=%d, RP=%d", moveDirection, tempStepperPosition, tempStepperRightPosition);
                Serial.printlnf(strLog);
            }
        }
    }
    if (abs(hiStepperRightPosition   - hiStepperPosition)   >= 1) 
    {
        moveDirection = (((hiStepperRightPosition - hiStepperPosition) > 0) - ((hiStepperRightPosition - hiStepperPosition) < 0));
        if ((hiStepperPosition + moveDirection) >= 0 && hiStepperPosition + moveDirection <= kTempDisplaySteps)
        {
            hiStepper.step(moveDirection);
            hiStepperPosition = hiStepperPosition + moveDirection;
        }
    }
    if (abs(dewStepperRightPosition  - dewStepperPosition)  >= 1) 
    {
        moveDirection = (((dewStepperRightPosition - dewStepperPosition) > 0) - ((dewStepperRightPosition - dewStepperPosition) < 0));
        
        if ((dewStepperPosition + moveDirection) >= 0 && dewStepperPosition + moveDirection <= kDewDisplaySteps)
        {
            dewStepper.step(moveDirection);
            dewStepperPosition = dewStepperPosition + moveDirection;
        }
    }
}



void registerFunctions()
{
    Particle.variable("TempC", tempC);
    Particle.variable("DewPointC", dewPointC);
    Particle.variable("HiTempC", hiTempC);
    Particle.variable("MakeActualCalls", makeActualCalls);
    
    bool success = false;
    success = Particle.function("set_polling_interval", setPollingInterval);
    success ? Serial.println("Registered set_polling_interval") : Serial.println("Failed to register polling_mojo");
    success = Particle.function("trim_temp", trimTempMotor);
    success ? Serial.println("Registered trim_temp") : Serial.println("Failed to register trim_temp");
    success = Particle.function("trim_hitemp", trimHiTempMotor);
    success ? Serial.println("Registered trim_hitemp") : Serial.println("Failed to register trim_hitemp");
    success = Particle.function("trim_dew", trimDewMotor);
    success ? Serial.println("Registered trim_dew") : Serial.println("Failed to register trim_dew");
    success = Particle.function("set_temp_needle", setTempMotor);
    success ? Serial.println("Registered set_temp_needle") : Serial.println("Failed to register set_temp_needle");
    success = Particle.function("set_hi_needle", setHiMotor);
    success ? Serial.println("Registered set_hi_needle") : Serial.println("Failed to register set_hi_needle");
    success = Particle.function("set_dew_needle", setDewMotor);
    success ? Serial.println("Registered set_dew_needle") : Serial.println("Failed to register set_dew_needle");
    success = Particle.function("set_city_code", setCityCode);
    success ? Serial.println("Registered set_city_code") : Serial.println("Failed to register set_city_code");
    success = Particle.function("set_api_key", setApiKey);
    success ? Serial.println("Registered set_api_key") : Serial.println("Failed to register set_api_key");
}

int setPollingInterval(String command)
{
     int newMs = command.toInt();
    if (newMs < 5000) 
    {
        // that's too small, let's be reasonable
        newMs = 900000;
    }
    pollingInterval = newMs;
    if (verbosity >= 1)
    {
        char strLog[45] = "";
        sprintf(strLog, "Set polling interval to %d ms", newMs);
        Particle.publish("Command", strLog, PRIVATE);
    }
    return 0;
}

int trimTempMotor(String command)
{
    // move the motor, but do not update the variable with its current position
    // useful to calibrate the needle on a running display
    int steps = command.toInt();
    tempStepper.step(steps);
    if (verbosity >= 1)
    {
        char strLog[25] = "";
        sprintf(strLog, "Moved Temp motor %d steps", steps);
        Particle.publish("Command", strLog, PRIVATE);
    }
    return 0;
}

int trimHiTempMotor(String command)
{
    // move the motor, but do not update the variable with its current position
    // useful to calibrate the needle on a running display
    int steps = command.toInt();
    hiStepper.step(steps);
    if (verbosity >= 1)
    {
        char strLog[25] = "";
        sprintf(strLog, "Moved Temp motor %d steps", steps);
        Particle.publish("Command", strLog, PRIVATE);
    }
    return 0;
}

int trimDewMotor(String command)
{
    // move the motor, but do not update the variable with its current position
    // useful to calibrate the needle on a running display
    int steps = command.toInt();
    dewStepper.step(steps);
    if (verbosity >= 1)
    {
        char strLog[25] = "";
        sprintf(strLog, "Moved Temp motor %d steps", steps);
        Particle.publish("Command", strLog, PRIVATE);
    }
    return 0;
}

int setTempMotor(String command)
{
    // set the temp motor to a temp (Celsius)
    int newTemp = command.toInt();
    tempStepperRightPosition = tempToPosition(newTemp);
    if (verbosity >= 1)
    {
        char strLog[25] = "";
        sprintf(strLog, "Set temp %d (%d)", newTemp, tempStepperRightPosition);
        Particle.publish("Command", strLog, PRIVATE);
    }
    return 0;
}

int setHiMotor(String command)
{
    // set the hi temp motor to a temp (Celsius)
    int newTemp = command.toInt();
    hiStepperRightPosition = tempToPosition(newTemp);
    if (verbosity >= 1)
    {
        char strLog[25] = "";
        sprintf(strLog, "Set hi temp %d (%d)", newTemp, hiStepperRightPosition);
        Particle.publish("Command", strLog, PRIVATE);
    }
    return 0;
}

int setDewMotor(String command)
{
    // set the dew point motor to a temp (Celsius)
    int newTemp = command.toInt();
    dewStepperRightPosition = dewToPosition(newTemp);
    if (verbosity >= 1)
    {
        char strLog[25] = "";
        sprintf(strLog, "Set dew point %d (%d)", newTemp, dewStepperRightPosition);
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
