#include <Particle.h>
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#include <ArduinoJson.h>
#include <HttpClient.h>
#include <Stepper.h>

// #include <math.h>

const float kMinDisplayTempC   = -4.0;  // record low temp  -8.89 C ( 16 F) as of May 30, 2019
const float kMaxDisplayTempC   = 52.0;  // record high temp 50.00 C (122 F)
const int   kTempDisplaySteps  = 450;   
const float kMinDisplayDewC    = -30.44;// record low dew point -30.55 C (-23 F)
const float kMaxDisplayDewC    = 26.67; // record high dew point 26.11 C ( 79 F)
const int   kDewDisplaySteps   = 360;


int verbosity = 2;            // 0: don't say much, 2: say lots
int makeActualCalls = 1;      // 1 means make real web calls by default, anything else uses a hard-coded JSON doc
int pollingInterval = 20000; //milliseconds
unsigned long lastPoll = 0 - pollingInterval; // last time we called the web service

double tempC = -273.15;
double hiTempC = -273.15;
double dewPointC = -273.15;
char apiKey[33] = "[YOUR WEATHERBIT.IO API KEY HERE]";

// variables to handle the once-a-day hi temp forecast reset
int lastHiTempReset = 0;      // weekday, 1=Sunday, 7=Saturday
int hiTempResetHour = 4;      // reset on the hour, this hour every day (0-23)

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
   
    request.hostname = "api.weatherbit.io";
    request.port = 80;

    // set up our steppers
    tempStepper.setSpeed(5);
    hiStepper.setSpeed(5);
    dewStepper.setSpeed(5);
    
    int i;
    for (i = 0; i < 630; i++) 
    {
        tempStepper.step(-1); // enough to be against the counterclockwise stop
        hiStepper.step(-1);
        dewStepper.step(-1);
    }
        
    tempStepper.step(21);  // enough to reach horizontal (0) on each motor
    hiStepper.step(88);
    dewStepper.step(84);
    
    tempStepperPosition = 0;
    hiStepperPosition = 0;
    dewStepperPosition = 0;
       
    delay(10000);
    //TODO: Take contol of the on-board LED
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
                hiTempC = kMinDisplayTempC;
                // but try again next time
                lastHiTempReset = 0;
            }
        }
        
        // every polling interval, refresh the current conditions data document & get temp + dew point
        int successfulConditionsSet = resetTempAndDewPoint();
        if (successfulConditionsSet == 0)
        {
            Serial.printlnf("SUCCESS: T %f C, D %f C", tempC, dewPointC);
        }
        else
        {
            Serial.println("at least one of temp and dewpoint was not set properly and I am sad.");
        }
        
        // set all motors' correct positions
        tempStepperRightPosition = tempToPosition(tempC);
        hiStepperRightPosition = tempToPosition(hiTempC);
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
    sprintf(conditionsRequestPath, "/v2.0/current?key=%s&city_id=5308049&units=M", apiKey);

    int conditionsRefreshed = refreshJson(conditionsRequestPath);
    if (conditionsRefreshed == 0) 
    {
        bool outOfBounds = false;
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
    // and set the hiTempC value
    int returnVal = 0;
    
    Serial.printlnf("resetting hi temp");
    char forecastRequestPath[100];
    sprintf(forecastRequestPath, "/v2.0/forecast/daily?key=%s&days=1&city_id=5308049&units=M", apiKey);
    
    int forecastRefreshed = refreshJson(forecastRequestPath);
    
    if (forecastRefreshed == 0) 
    {
        Serial.println("forecast refresh successful.");
    
        double htC = kMinDisplayTempC; // if this all fails, display min temp on the scale
        htC = weatherDoc["data"][0]["max_temp"];
        Serial.printlnf("hi temp is now %f", htC);
        if (htC > -10 && htC < 55) hiTempC = htC;
    }
    else
    {
        Serial.println("forecast refresh failed, will try again next polling interval");
        returnVal = -1;
    }
    return returnVal;
}



int refreshJson(String requestPath)
{
    // use the global httpclient to request a path
    // requestType should be 1 for current conditions, 2 for forecast
    //String conditionsPath = "/v2.0/current?key=5b822cc6043144ed98612881846d5d51&city_id=5308049&units=M";
    //String forecastPath = "/v2.0/forecast/daily?key=5b822cc6043144ed98612881846d5d51&days=1&city_id=5308049&units=M";

    int returnVal = 0;

    if (makeActualCalls == 1)
    {
        //request.path = "/v2.0/current?key=5b822cc6043144ed98612881846d5d51&city_id=5308049&units=M";
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
            //TODO: pull temp and dewpoint out of JSON
        }
        else
        {
            Serial.printlnf("ERROR");
            Serial.printlnf(response.body);
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
    
    newPosition = newPosition + 0.5 - (newPosition<0);
    returnPos = (int)newPosition;
    if (returnPos < 0) returnPos = 0;
    if (returnPos > scaleSteps) returnPos = scaleSteps;
    
    return returnPos;
}

void adjustMotors()
{
    // to be called every loop
    // if any motor is a step or more away from its correct position, take one step in the right direction
        // signbit: 1 if arg is negative, 0 otherwise
        // if RightPosition > Position, we want + movement and signbit(RightPosition - Position) == 0
        // if RightPosition < Position, we want - movement and signbit(Rightposition - Position) == 1
        // so moveDirection = (-2 * signbit(RightPosition - Position)) + 1;

    //TIWIAGTH: there's a bug in here, all 3 values run away toward positive
    int moveDirection;
    if (abs(tempStepperRightPosition - tempStepperPosition) >= 1) // if motors are a step or more away from their correct position
    {

        char strLog[45] = "";
        sprintf(strLog, "Before: P=%d RP=%d", tempStepperPosition, tempStepperRightPosition);
        Serial.printlnf(strLog);

        moveDirection = (-2 * signbit(tempStepperRightPosition - tempStepperPosition)) + 1; 
        tempStepper.step(moveDirection);
        tempStepperPosition = tempStepperPosition + moveDirection;
        
        sprintf(strLog, "M=%d, P=%d, RP=%d", moveDirection, tempStepperPosition, tempStepperRightPosition);
        Serial.printlnf(strLog);
    }
    if (abs(hiStepperRightPosition   - hiStepperPosition)   >= 1) 
    {
        moveDirection = (-2 * signbit(hiStepperRightPosition - hiStepperPosition)) + 1;
        hiStepper.step(moveDirection);
        hiStepperPosition = hiStepperPosition + moveDirection;
    }
    if (abs(dewStepperRightPosition  - dewStepperPosition)  >= 1) 
    {
        moveDirection = (-2 * signbit(dewStepperRightPosition - dewStepperPosition)) + 1;
        dewStepper.step(moveDirection);
        dewStepperPosition = dewStepperPosition + moveDirection;
    }
}

void registerFunctions()
{
    Particle.variable("TempC", tempC);
    Particle.variable("DewPointC", dewPointC);
    Particle.variable("HiTempC", hiTempC);
    Particle.variable("MakeActualCalls", makeActualCalls);
    
    bool success = false;
    success = Particle.function("polling_mojo",   setPollingInterval);
    success ? Serial.println("Registered polling_mojo") : Serial.println("Failed to register polling_mojo");
    success = Particle.function("trim_temp", trimTempMotor);
    success ? Serial.println("Registered trim_temp") : Serial.println("Failed to register trim_temp");
    success = Particle.function("trim_hitemp", trimHiTempMotor);
    success ? Serial.println("Registered trim_hitemp") : Serial.println("Failed to register trim_hitemp");
    success = Particle.function("trim_dew", trimDewMotor);
    success ? Serial.println("Registered trim_dew") : Serial.println("Failed to register trim_dew");
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

int trimTempMotor(String command)
{
    //TODO: add error handling
    double steps = command.toFloat();
    tempStepper.step(steps);
    if (verbosity >= 1)
    {
        char strLog[25] = "";
        sprintf(strLog, "Moved Temp motor %3.1f steps", steps);
        Particle.publish("Command", strLog, PRIVATE);
    }
    return 0;
}

int trimHiTempMotor(String command)
{
    //TODO: add error handling
    double steps = command.toFloat();
    hiStepper.step(steps);
    if (verbosity >= 1)
    {
        char strLog[25] = "";
        sprintf(strLog, "Moved Temp motor %3.1f steps", steps);
        Particle.publish("Command", strLog, PRIVATE);
    }
    return 0;
}

int trimDewMotor(String command)
{
    //TODO: add error handling
    double steps = command.toFloat();
    dewStepper.step(steps);
    if (verbosity >= 1)
    {
        char strLog[25] = "";
        sprintf(strLog, "Moved Temp motor %3.1f steps", steps);
        Particle.publish("Command", strLog, PRIVATE);
    }
    return 0;
}





