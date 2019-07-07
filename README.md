# WeatherMojo
IoT project to get weather information from the web and display it on a weather station. 

It periodically downloads data and sets the position of stepper motors to display weather information on analog gauges. It's built for a [Particle Photon](https://www.particle.io/wifi/) running [DeviceOS 1.2.0-beta-1](https://github.com/particle-iot/device-os/releases/tag/v1.2.0-beta.1).

Build details at https://www.powerfulmojo.com/weather/
Particle function and variables are documented at https://www.powerfulmojo.com/weathermojo/

## Files
* [forecastmojo.ino](./forecastmojo.ino) : Firmware to run a weather station showing forecast hi temp
* [weathermojo.ino](./weathermojo.ino) : Firmware to run a weather station showing observed hi temp
* [steppertest.ino](./steppertest.ino) : Firmware to mess with your stepper motors for testing and calibration
* [epapermojo.ino](./epapermojo.ino) : Firmware to run a separate e-paper weather station

The [weathermojo](weathermojo.ino) and [forecastmojo](forecastmojo.ino) firmwares use different weather APIs and use the high temperature stepper motor differently.<br />[forecastmojo.ino](./forecastmojo.ino) uses [weatherbit.io](http://weatherbit.io/) and displays today's forecast high.<br />[weathermojo.ino](./weathermojo.ino) uses [openweathermap.org](http://openweathermap.org/) and displays the highest temperature so far today.<br /><br />The [epapermojo.ino](./epapermojo.ino) firmware powers a separate e-paper-based weather station. Depending on its configuration, it can get weather from the web all by itself, or it can subscribe to updates from the  [forecastmojo.ino](./forecastmojo.ino) device.

## Weather Forecasting Firmware ([forecastmojo.ino](./forecastmojo.ino))
Retrieves weather conditions and forecast from [weatherbit.io](http://weatherbit.io). Displays current temperature on the "big hand" of the temperature dial. Displays the forecast high temperature on a "needle" on the same dial. Displays the dew point on a separate small dial.<br />
<br />Sets current temperature dewpoint every 15 minutes by default. You can make updates more or less frequently by calling **set_polling_interval(milliseconds)**. <br />
<br />Sets the forecast high temperature once daily at 4:00am Arizona time. Changing the time zone requires recompiling the firmware. The station doesn't do anything about daylight saving time because we don't play that game in Arizona.

### Particle Variables
* **TempC** The current temperature in Celsius
* **DewPointC** The current dew point in Celsius
* **HiTempC** The last updated high temperature in Celsius
* **LastUpdate** The time of the last temperature and high temperature update

### Particle Functions
* int **set_polling_interval**(String _Command_) <br /> 
Sets the polling interval to _Command_ ms (minimum 5000). Returns **0** on success. If you give it a non-integer or a number less than 5000, it sets the polling interval to the default value of 900,000 ms.<br />
* int **trim_temp**(String _Command_) <br />
Moves the temperature needle _Command_ steps, but does not update the **TempC** variable. Negative numbers go counterclockwise, positive go clockwise. Returns **0** on success.<br />
* int **trim_hitemp**(String _Command_) <br />
Moves the high temperature needle _Command_ steps, but does not update the **HiTempC** variable. Negative numbers go counterclockwise, positive go clockwise. Returns **0** on success.<br />
* int **trim_dew**(String _Command_) <br />
Moves the dew point needle _Command_ steps, but does not update the **DewPointC** variable. Negative numbers go counterclockwise, positive go clockwise. Returns **0** on success.<br />
* int **set_temp_needle**(String _Command_) <br />
Sets the current temperature to _Command_ degrees C. The needle position will be updated accordingly. Returns 0 on success. If the dial isn’t calibrated correctly don’t use this to fix it. It will just get over-written with the next weather update. You probably want to use **trim_temp** to change the station’s idea of where to point when showing the current temperature.<br />
* int **set_hi_needle**(String _Command_) <br />
Sets the forecast high temperature to _Command_ degrees C. The needle position will be updated accordingly. Returns 0 on success. If the dial isn’t calibrated correctly don’t use this to fix it. It will just get over-written tomorrow. You probably want to use **trim_hitemp** to change the station’s idea of where to point when showing the hi temperature.<br />
* int **set_dew_needle**(String _Command_) <br />
Sets the dew point temperature to _Command_ degrees C. The needle position will be updated accordingly. Returns 0 on success. If the dial isn’t calibrated correctly don’t use this to fix it. It will just get over-written with the next weather update. You probably want to use **trim_dew** to change the station’s idea of where to point when showing the dew point.<br />
* int **set_city_code**(String _Command_) <br />
Sets the city code to use for weather conditions. A full list is at https://www.weatherbit.io/api/meta. Returns 0 on success. Setting it to an invalid value will cause the WeatherMojo to stop updating temperatures.<br />
* int **set_api_key**(String _Command_) <br />
Sets the [weatherbit.io] API key to use for getting weather conditions. Sign up for one at https://www.weatherbit.io/account/create. Returns 0 on success. Setting it to an invalid value will cause the WeatherMojo to stop updating temperatures.<br />

## Dependencies
WeatherMojo includes these libraries:
* [Stepper](https://github.com/arduino-libraries/Stepper) by [facchinm](https://github.com/facchinm)
* [HttpClient](https://github.com/nmattisson/HttpClient) by [nmattisson](https://github.com/nmattisson)
* [ArduinoJson](https://github.com/bblanchon/ArduinoJson) by [bblanchon](https://github.com/bblanchon)
