A simple ESP32-s3 based project that broadcasts data as per drone remoteID regulations. 

It can use a NMEA compliant GPS module or Mavlink position data from your flight controller's GPS module


The project builds on the excellent project by https://github.com/VOLTEKOVER/ESP_DRONE_REMOTEID

Some features:

WebUI has a mobile and desktop layout, auto detected  and served by firmware 

OTA firmware update

Webserver disable option / Wifi password / web interface password protected

Buzzer output to alert that GPS lock was achieved and home position saved for the session

LED status/buzzer pins can be re-mapped via webUI


## Also in this repo

[`GoldSquawkWidget/`](./GoldSquawkWidget) - an unrelated Android home-screen
widget that shows a live gold price from TradingView and notifies on
bull/bear trend flips. See its own README for details.

