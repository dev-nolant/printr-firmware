# printr

Firmware for a WiFi thermal printer: an ESP8266 (NodeMCU or Wemos D1 mini) wired to a cheap Adafruit-style 58 mm TTL receipt printer. Send it text from a browser, a script, or a server, and it prints.

- Web page and JSON API on your network (`http://printr.local`)
- Handles real-world text: accents and symbols are converted to the printer's code page, emoji become ASCII stand-ins, words wrap cleanly
- Works mounted upside down (receipts still read top to bottom)
- WiFi setup from your phone, firmware updates over WiFi
- Optional server connection for sending messages from anywhere

## Wiring

| ESP8266 | Printer       |
|---------|---------------|
| D5      | RX (data in)  |
| D6      | TX (data out, optional: needed for paper-out detection) |
| GND     | GND           |

Power the printer from its own 5–9 V supply that can deliver at least 2 A. It must not
be powered from the ESP8266. To change the pins, edit `PIN_PRINTER_TX` and `PIN_PRINTER_RX` in `config.h`.

## Build and flash

Toolchain (known good versions):

- ESP8266 Arduino core **3.1.x**
- ArduinoJson **7.x**. Version 6 won't compile.
- WiFiManager (tzapu) **2.0.x**
- Adafruit Thermal Printer Library **1.4.x**

**Arduino IDE:** open `printr/printr.ino` and select board *NodeMCU 1.0 (ESP-12E Module)*.
Set Flash Size to *4MB (FS:2MB OTA:~1019KB)*, which is the default. Then upload.

**arduino-cli:** `sh build.sh` compiles, and `sh build.sh COM5` compiles and flashes.

The first flash must be over USB. After that, you can update over WiFi (see below).

## First-time setup

1. Power on. The printer prints **WiFi setup** instructions.
2. On your phone, join the WiFi network `printr-setup-XXXX`. It has no password.
3. Pick your home WiFi and enter its password. The *Cloud server URL* field can be left blank.
4. The printer connects and prints an **online** receipt with its address.

Keep the setup receipt: it shows the **admin password**. You can also read the
password over USB serial with the `I` command.

If the saved WiFi stays unreachable for 10 minutes (for example, you changed
routers), the setup network turns on again automatically. The printer keeps
trying the saved WiFi in the meantime, so a router reboot or power cut doesn't
leave it stuck in setup mode.

## Using it

Open `http://printr.local` (or its IP address) in a browser on the same network.
From there you can print messages, see status, run tests, and change settings.

### HTTP API

```sh
# Print (JSON)
curl -X POST http://printr.local/api/print \
     -H 'Content-Type: application/json' \
     -d '{"text":"Hello from the API 👋","from":"Me"}'

# Print (plain text)
curl -X POST http://printr.local/api/print --data-binary @note.txt

# Status
curl http://printr.local/api/status

# Change settings (admin)
curl -u admin:PASSWORD -X POST http://printr.local/api/settings \
     -H 'Content-Type: application/json' -d '{"timezone":"EST5EDT,M3.2.0,M11.1.0"}'

# Actions (admin): test, chartest, poll, heartbeat, register, markall,
#                  portal, reboot, forget-wifi, factory-reset
curl -u admin:PASSWORD -X POST http://printr.local/api/action/test
```

| Response | Meaning |
|---|---|
| `200` | printed |
| `400` | empty or invalid request |
| `413` | message too long (the limit is 4096 bytes) |
| `503` | printer is out of paper |

By default, anyone on your LAN can print. Set `requireAuthToPrint` if you want
printing to need the admin password too.

Other websites can't print or change settings. Cross-origin browser requests
are always refused.

### Settings

| Setting | Default | Notes |
|---|---|---|
| `deviceName` | `printr` | `NAME.local`. Takes effect after a reboot. |
| `adminPassword` | random | Takes effect after a reboot. |
| `requireAuthToPrint` | `false` | |
| `upsideDown` | `true` | The printer is mounted upside down. Receipts still read top to bottom. |
| `lineWidth` | `32` | Characters per line (58 mm paper). |
| `printerBaud` | `9600` | Takes effect after a reboot. Some printers use 19200. |
| `heatDots` / `heatTime` / `heatInterval` | `7` / `80` / `2` | Print darkness and speed. |
| `printDensity` / `printBreakTime` | `10` / `3` | |
| `feedLines` | `4` | Blank lines after each receipt. Pushes the last printed part (the header, when mounted upside down) past the tear bar. |
| `bootBanner` | `true` | Print an "online" receipt after power-on or the reset button. Over-the-air updates and software reboots stay quiet. |
| `checkPaper` | `true` | Hold messages while paper is out (needs the D6 wire). |
| `timezone` | *(blank)* | POSIX TZ string. When set, messages print with a timestamp. |
| `serverUrl` | *(blank)* | Blank means local only. Changing it clears the cloud credentials. |
| `tlsInsecure` | `false` | Skip HTTPS certificate checks. Only for test servers. |
| `pollSeconds` / `heartbeatSeconds` | `5` / `60` | |

### Firmware updates over WiFi

- **Script:** `sh build.sh ota printr.local` builds and uploads in one step.
  It asks for the admin password, or reads it from `PRINTR_PASSWORD`.
- **Browser:** open `http://printr.local/update` and log in as `admin`.
  Upload the `.bin` file (Arduino IDE: *Sketch > Export Compiled Binary*).
- **Arduino IDE:** the printer appears as a network port. It asks for the admin password.

### Serial console

Connect at 115200 baud and send a command followed by Enter. `?` lists all commands.

| Command | Action |
|---|---|
| `I` | device info, including the admin password |
| `S` | status: memory, WiFi, paper, cloud |
| `T` | print a test page |
| `P` | print the character test |
| `U <url>` | set the server URL |
| `C` | open WiFi setup |
| `W` | forget WiFi |
| `B` | reboot |
| `Z CONFIRM` | factory reset |

## Server connection (optional)

Set `serverUrl` (in the setup portal, the web UI, or with `U https://...` on
serial) and the printer will also pick up messages from a server. It talks
plain HTTPS + JSON, so any backend that implements these endpoints works:

| Request | Purpose |
|---|---|
| `POST /api/get-pairing-code` | Register. Body: `device_id`, `device_secret`, `name`, `firmware`. Returns `api_key`, `display_code`, and a `pairing_code` the printer prints so a user can claim it. |
| `GET /api/messages/pending` | Poll for messages (every `pollSeconds`). Returns `{"messages": [{"id", "message_text", "sender_name", "created_at"}]}`. |
| `POST /api/messages/mark-specific-printed` | Acknowledge printed messages: `{"message_ids": [...]}`. |
| `POST /api/heartbeat` | Status every `heartbeatSeconds`: uptime, free memory, signal, firmware. |

Authenticated requests carry `X-API-Key` and `X-Device-ID` headers. A `401`
or `403` three times in a row makes the printer drop its credentials and
register again.

The `device_secret` is random and generated on first boot (it survives a
factory reset). A server should bind it to the device ID on first
registration, so knowing a printer's ID (it's derived from the MAC address)
isn't enough to take over its account.

Delivery is at-least-once with de-duplication: each message is acknowledged
after it prints, and recently printed IDs are remembered so a lost
acknowledgement doesn't cause a second print. While the printer is out of
paper, messages stay on the server.

HTTPS certificates are verified against built-in Let's Encrypt and Google
Trust Services roots, which covers most hosts. If your server uses a different
CA, paste its root certificate into the web UI. Plain `http://` works for
servers on your LAN.

## Development

The text-handling code (character conversion and word wrap) has host-side unit tests:

```sh
sh test/run_tests.sh      # MinGW without a plain `ld`: CXXFLAGS=-fuse-ld=bfd sh test/run_tests.sh
```

## License

MIT
