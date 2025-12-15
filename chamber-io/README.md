# chamber-io

esp32 based I/O controller

## Specs

- Inputs
  - 1 I2C port, for a Temperature / Humidity sensor
    - currently supported
      - DHT20
      - AM2301B
  - 1 Digital input port, for a floating sensor
- Outputs
  - 1 I2C port, for a Temperature + Humidity sensor, currently supported
    - DHT20
    - AM2301B
  - 3 Digital output ports, for external relays or something
  - 1 Digital output port with external DC power supply, for water pumps
  - 1 PWM / Servo output port
- Networking support
  - Publishing periodically sensor values via OSC (OpenSoundControl)

---

TBA more description
