# WeatherMojo
IoT project to get weather information from the web and display it on a weather station. 

It periodically downloads data and sets the position of stepper motors to display weather information on analog gauges. It's built for a [Particle Photon](https://www.particle.io/wifi/) running [DeviceOS 1.2.0-beta-1](https://github.com/particle-iot/device-os/releases/tag/v1.2.0-beta.1).

Build details at https://www.powerfulmojo.com/weather/

Files:
* [weathermojo.ino](./weathermojo.ino) : Firmware to run a weather station
* [forecast_hi_mojo.ino](./forecast_hi_mojo.ino) : Firmware to run a weather station
* [steppertest.ino](./steppertest.ino) : Firmware to mess with your stepper motors for testing and calibration

The two firmwares use different weather APIs and use the high temperature stepper motor differently. [weathermojo.ino](./weathermojo.ino) uses [openweathermap.org](http://openweathermap.org/) and displays the highest temperature it has seen today. [forecast_hi_mojo.ino](./forecast_hi_mojo.ino) uses [weatherbit.io](http://weatherbit.io/) and displays today's forecast high.

It depends on:
* [Stepper](https://github.com/arduino-libraries/Stepper) by [facchinm](https://github.com/facchinm)
* [HttpClient](https://github.com/nmattisson/HttpClient) by [nmattisson](https://github.com/nmattisson)
* [ArduinoJson](https://github.com/bblanchon/ArduinoJson) by [bblanchon](https://github.com/bblanchon)



