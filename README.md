### Solarlife

This is a simple approach to read, parse and pass data from cheap solar charge controllers like Lumiax and Volt to MQTT broker.
You can pass data to home automation software like Home Assistant, Openhab and others.

Parameters that you will be able to get are for example:

- battery voltage
- battery current
- battery power
- battery percentage capability
- solar voltage
- solar current
- solar power
- load voltage
- load current
- load power
- and many more...


Based on:
- https://github.com/majki09/lumiax_solar_bt/tree/main
- https://github.com/gbrault/gattclient/tree/master
- https://ctlsys.com/support/how_to_compute_the_modbus_rtu_message_crc/
- https://github.com/kokke/tiny-MQTT-c
- https://github.com/rpz80/json
- Lumiax Modbus Communication Protocol V3.9-1.pdf

Depends on:
    libglib-2.0

Tested on VM running Ubuntu 21.04 and Fritz-Box 6690 Cable router.

To use this app you will need a Bluetooth adapter with low energy capability and compatible solar charger with Bluetooth.
Generally speaking all of charging controllers that uses "Solar Life" app for Android should work.


TODO:
- Fix bugs
- Pass commands to controller (load on/off, clock setup, etc.)

Example output:
```console
root@ubuntu:/usr/src/solarlife# ./solarlife -d 04:7f:0e:51:21:4c -a test.mqtt.com
Connecting to BT device... Done
MQTT: connected to server.
Service Added - UUID: 00001801-0000-1000-8000-00805f9b34fb start: 0x0001 end: 0x0003
Service Added - UUID: 00001800-0000-1000-8000-00805f9b34fb start: 0x0004 end: 0x000e
Service Added - UUID: 0000ff00-0000-1000-8000-00805f9b34fb start: 0x000f end: 0x001a
GATT discovery procedures complete
Registering notify handler with id: 2
Registered notify handler!
{"PV_rated_voltage":50,"PV_rated_current":10,"PV_rated_power_l":130,"PV_rated_power_h":0,"battery_rated_voltage":17,"battery_rated_current":10,"battery_rated_power_l":130,"battery_rated_power_h":0,"load_rated_voltage":17,"load_rated_current":10,"load_rated_power_l":130,"load_rated_power_h":0,"slave_id":1,"running_days":6,"sys_voltage":12,"battery_status":3,"charge_status":32,"discharge_status":0,"env_temperature":13,"sys_temperature":6,"undervoltage_times":255,"fullycharged_times":1,"overvoltage_prot_times":0,"overcurrent_prot_times":0,"shortcircuit_prot_times":0,"opencircuit_prot_times":0,"hw_prot_times":0,"charge_overtemp_prot_times":0,"discharge_overtemp_prot_times":0,"battery_remaining_capacity":16,"battery_voltage":11.13,"battery_current":0,"battery_power_lo":0,"battery_power_hi":0,"load_voltage":0,"load_current":0,"load_power_l":0,"load_power_h":0,"solar_voltage":0.2,"solar_current":0,"solar_power_l":0,"solar_power_h":0,"daily_production":0.01,"total_production_l":0.82,"total_production_h":0,"daily_consumption":0.02,"total_consumption_l":0.83,"total_consumption_h":0,"lighttime_daily":695,"monthly_production_l":0,"monthly_production_h":0,"yearly_production_l":0,"yearly_production_h":0,"timestamp":"2023-10-20T19:15:58Z"}
MQTT server acknowledged data
```
Data presented by openHAB app:
<br><img src="https://raw.githubusercontent.com/majonezz/solarlife/main/Solarlife_OH1.jpg" width="300"><img src="https://raw.githubusercontent.com/majonezz/solarlife/main/Solarlife_OH2.jpg" width="300"><br>
Compilation:
- make
