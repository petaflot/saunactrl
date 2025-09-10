Some electronics to control a sauna, with PID, web server.

# Features

* PID control
* remote enable/disable (see below)
* independant phase control


## TODO

* RGB light (connect to WLED?)
* ice bath
* door switch(es)
* Timing bell and deadman's switch
* remote set temp
* PS-VM-RD integration


# Hardware Stuff

## Pin assignments:
```
ESP   label       PS-VM-RD
--------------------------
D0    WS2812_Din  -
D1    OneWire     -
D2    RAON        7
D3    SSR_T       -
D4    LED         -
D5    SCLK        15
D6    (MISO) SDO  14
D7    (MOSI) SDI  13
D8    (CS) LATCH  16
RX    SSR_S       -
TX    SSR_R       -
A0    AOUTA       12
```

# Software Stuff

## Compile, upload and monitor

```sh
pio run --target uploadfs
pio run --target upload
pio device monitor
```

## Remote operation

### Enable

`curl http://<ip>/enable`

### Disable

`curl http://<ip>/disable`


