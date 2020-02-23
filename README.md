# About

`carbon` is an Arduino C++11 sketch for temperature, humidity, and CO2 measurement device ZG01 (MasterKit MT8060). It provides a simple program for MCU ESP8266 for collecting and sending metrics to the MQTT broker. This project is a part of health measurement system for office workers.

## Features

- [x] WPA2 PSK support
- [x] MQTT authentication
- [ ] MQTT TLS encryption
- [x] mDNS configuration (ZeroConf)
- [x] HTTP API for management
- [x] OTA upgrades support

## MQTT message format

`carbon` sends MQTT message in the following format:

```
/devices/MT8060/<name>/<metrics> {value}
```

## HTTP API

Read configuration from device.

```
curl -sL http://esp-50-02-91-48-02-68.local/config/read
```

Set up device configuration. It works unless device rebooted.

```
curl -X POST http://esp-50-02-91-48-02-68.local/config/apply -d '{"device_id": "red-room"}'
```

Save settings permanently.

```
curl -X POST http://esp-50-02-91-48-02-68.local/config/commit
```

## License

Released under the MIT license (see [LICENSE](LICENSE))

[![Sponsored by FunBox](https://funbox.ru/badges/sponsored_by_funbox_grayscale.svg)](https://funbox.ru)
