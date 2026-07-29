A simple ESP32-s3 based project that broadcasts data as per drone remoteID regulations. 

Very useful as it can receive mavlink data from your flight controller's gps instead of needing a second GPS module

The project builds on the excellent project by https://github.com/VOLTEKOVER/ESP_DRONE_REMOTEID

Some features:

WebUI has a mobile and desktop layout, auto detected  and served by firmware 

OTA firmware update

Webserver disable option / Wifi password / web interface password protected

Buzzer output to alert that GPS lock achieved and home position saved for the session

LED status/buzzer pins can be re-mapped via webUI



