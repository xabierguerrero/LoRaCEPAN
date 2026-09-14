# Setup Dev

## IDE

The IDE used for this project is ArduinoIDE. To install it go to [Arduino's downloads page](https://www.arduino.cc/en/software/).

## Boards Manager

> Before plugging in your board make sure to connect the antenna, else the board may malfunction.

This step requires to install a non default board manager, so go to `File > Preferences` and add
the following link to the `Aditional boards manager URLs`:

```
https://resource.heltec.cn/download/package_heltec_esp32_index.json
```

This allows the installation of the `Heltec ESP32 Series Arduino Develop Environment` by Heltec Automation (v3.3.8) boards manager

Finally, with the board plugged in, set it's type to `WiFi LoRa 32 (v4 R8)` in the `Tools > Board: this name may vary > Heltec 
ESP32 Series Arduino Develop Environment > WiFi LoRa 32 (v4 8R)`

## Libraries

The code requires multiple libraries installed to be able to be compiled. To install a library go to
the `library manager` in the left side panel's third tab or `Sketch > Include Library > Manage Libraries`
if this panel is hidden.

### Library list:

- `RadioLib` by Juan Gromes (v7.7.1)
- `U8g2` by oliver (v2.36.19)

## Compile and upload the code

To compile the code go to `Sketch > verify/compile`, hit `ctrl + r` or click the `✓` in the top left corner inside the IDE.
To upload the code go to the same Sketch menu and click `Upload`, hit `ctrl + u` or click the `->` next to the `✓` icon.

## Set the permission for the board's port (Linux)

1. Check the owner of `/dev/ttyACM0`. It should be owned by `uucp` (Fedora/Nobara) or `dialout` (Arch/Endeavour):

```bash
ls -l /dev/ttyACM0
```

2. Check if the user is part of the group with the command `groups`.

3. If the user isn't part of the corresponding group run:

> Remember to swap \<port_group\> with the corresponding group name.

```bash
sudo usermod -aG <port_group> $USER
```

4. If the user was just added to the group, log out and log back in to apply the changes.

