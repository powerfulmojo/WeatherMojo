# WeatherMojo
IoT project to get weather information from the web and display it on a weather station. The project is developed for a Particle Photon running Device OS 1.2.0-beta.1

It periodically downloads OpenWeatherMap data and sets the position of stepper motors to display weather information on analog gauges. It's built for a Particle Photon running [DeviceOS 1.2.0-beta-1](https://github.com/particle-iot/device-os/releases/tag/v1.2.0-beta.1).

Files:
* [weathermojo.ino](./weathermojo.ino) : Firmware to run a weather station
* [steppertest.ino](./steppertest.ino) : Firmware to mess with your stepper motors for testing and calibration

It depends on:
* [Stepper](https://github.com/arduino-libraries/Stepper) by [facchinm](https://github.com/facchinm)
* [HttpClient](https://github.com/nmattisson/HttpClient) by [nmattisson](https://github.com/nmattisson)
* [ArduinoJson](https://github.com/bblanchon/ArduinoJson) by [bblanchon](https://github.com/bblanchon)

This work is licensed under a Creative Commons Attribution 4.0 International License.
https://creativecommons.org/licenses/by/4.0/
