#include <Particle.h>
#include <Stepper.h>

/*

A test harness to control the stepper motors connected to the WeatherMojo device.

Registers 3 particle functions:
     temp_step : move the temperature motor
     hitemp_step : move the hi temperature motor
     dew_step : move the deww point motor
*/

const int stepsPerRevolution = 630;

// Set up the gauge motors
Stepper tempStepper(720, D1, D0, D2, D3); // X40 inner ring
int tempStepperPosition = 0;

Stepper hiStepper(720, D5, D4, D6, D7); // X40 outer ring
int hiStepperPosition = 0;

Stepper dewStepper(720, A0, A1, A2, A3); // X27
int dewStepperPosition = 0; 

void setup() {
     tempStepper.setSpeed(20);
     hiStepper.setSpeed(20);
     dewStepper.setSpeed(20);
     
     bool success = Particle.function("temp_step", innerSteps);
     if (success) { Particle.publish("function", "motor_step function registered successfully", PUBLIC); }
     else { Particle.publish("function_error", "motor_step function registration failed", PUBLIC); }
     
     success = Particle.function("hitemp_step", outerSteps);
     if (success) { Particle.publish("function", "motor_step function registered successfully", PUBLIC); }
     else { Particle.publish("function_error", "motor_step function registration failed", PUBLIC); }
     
     success = Particle.function("dew_step", dewSteps);
     if (success) { Particle.publish("function", "motor_step function registered successfully", PUBLIC); }
     else { Particle.publish("function_error", "motor_step function registration failed", PUBLIC); }
}

void loop() {
     delay(10);
}

int innerSteps(String command)
{
    //TODO: add error handling
    float steps = command.toFloat();
    tempStepper.step(steps);
    return 0;
}

int outerSteps(String command)
{
    float steps = command.toFloat();
    hiStepper.step(steps);
    return 0;
}

int dewSteps(String command)
{
    float steps = command.toFloat();
    dewStepper.step(steps);
    return 0;
}
