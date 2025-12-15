# chamber-io

Arduino - ESP32 based I/O controller for automation of our mushroom chambers

## Specs

We are currently using seeed studio XIAO ESP32C6

- Inputs
  - 1 I2C port, for a Temperature / Humidity sensor
    - currently supported
      - DHT20
      - AM2301B
  - 1 Digital input port, for a floating sensor
- Outputs
  - 3 Digital output ports, for external relays or something
  - 1 Digital output port with external DC power supply, for water pumps
  - 1 PWM / Servo output port
- Networking support
  - Publishing sensor values to the host computer periodically, via UDP / OSC (OpenSoundControl)

---

TBA more description
