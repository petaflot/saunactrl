Some electronics to control a sauna, with PID, web server and a few other things

# Features

* PID control


## TODO
* Timing bell and deadman's switch


# Hardware Stuff

## Pin assignments:
```
D1  OneWire
D2  WS2812
D5  R
D6  S
D7  T
```

# Software Stuff

## Compile, upload and monitor

```sh
pio run --target upload
pio device monitor
```
