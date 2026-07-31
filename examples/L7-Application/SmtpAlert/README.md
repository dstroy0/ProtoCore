# SmtpAlert - make your ESP32 send an email

This example makes a tiny `$5` computer (the ESP32) **send you an email** - for example,
"the temperature is too high!" or "the door just opened." It is written so that a
complete beginner can follow it. If you have never set up a mail server before, that is
totally fine: we build one together, one step at a time.

Take your time. Nothing here can break your computer, and you can redo any step.

---

## What is going on here? (the big picture)

When you send an email, three things are involved:

1. **The sender** - here, your ESP32. It writes the message.
2. **A mail server** - a program on a computer whose whole job is to receive email and
   put it in a mailbox. The famous one that runs Gmail-sized systems, and also runs
   happily on a Raspberry Pi, is called **Postfix**. Think of it as your own little
   **post office**.
3. **The mailbox** - a file on the mail server where the message lands, so you can read it.

The ESP32 talks to the mail server using a set of rules called **SMTP** (Simple Mail
Transfer Protocol). SMTP is just a polite back-and-forth conversation: "Hello!" - "Hi!"

- "I have a letter from A to B" - "OK, go ahead" - "here it is" - "got it, thanks!" This
  example handles all of that for you.

```
  ESP32  ── "here is an email" (SMTP) ──▶  Postfix mail server  ──▶  a mailbox you can read
 (sender)                                     (your post office)
```

---

## What you will need

- An **ESP32 board** and a USB cable (any ESP32 dev board works).
- The **Arduino IDE** or **PlatformIO** installed, with this library added.
- Your **WiFi name and password**.
- A **computer to be the mail server**. A **Raspberry Pi** is perfect and cheap, but any
  computer running **Linux** or **macOS** works. It must be on the **same WiFi/network**
  as the ESP32.

You do **not** need a Gmail account, a domain name, or any paid service. Everything here
stays on your own network.

---

## Part 1 - Set up your own mail server (Postfix)

We will install Postfix on a Raspberry Pi (the steps are the same on any Debian/Ubuntu
Linux; on macOS, Postfix is already installed - skip to step 4 and edit
`/etc/postfix/main.cf`).

**Step 1. Open a terminal on the Raspberry Pi.** (On the Pi itself, or from another
computer with `ssh pi@<the-pi-ip>`.) A "terminal" is the black window where you type
commands.

**Step 2. Install Postfix.** Copy-paste this and press Enter:

```bash
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y postfix mailutils
```

- `sudo` means "do this as the administrator" (it may ask for your password - type it;
  the characters stay invisible, that is normal).
- If a blue setup screen appears, choose **"Internet Site"** and accept the default name.

**Step 3. Let the mail server listen to the network.** By default Postfix only listens to
itself. These three commands tell it to accept mail from your LAN:

```bash
sudo postconf -e 'inet_interfaces = all'
sudo postconf -e 'inet_protocols = ipv4'
sudo postconf -e 'mynetworks = 127.0.0.0/8 192.168.0.0/16'
```

`postconf -e` just edits Postfix's settings file for you. `mynetworks` is the list of
addresses Postfix trusts - `192.168.0.0/16` means "every device on my home network."

**Step 4. Restart Postfix so the changes take effect:**

```bash
sudo systemctl restart postfix
```

**Step 5. Check it is listening.** Run:

```bash
ss -tlnp | grep :25
```

You should see a line containing `0.0.0.0:25`. Port **25** is the standard "front door"
for email. If you see it, your post office is open!

**Step 6. Find the Pi's address and know your mailbox.** Run `hostname -I` - the first
number (e.g. `192.168.1.50`) is the Pi's **IP address**; write it down. Your mailbox is
your Linux username (for a Pi that is often `pi`); the ESP32 will send to
`<username>@<hostname>` - for example `pi@raspberrypi.local`. You can see your username
with `whoami` and the hostname with `hostname`.

---

## Part 2 - Tell the ESP32 about your mail server

Open [SmtpAlert.ino](SmtpAlert.ino) and edit the lines marked `CHANGE ME`:

```cpp
static const char *SSID     = "YOUR_SSID";        // your WiFi name
static const char *PASSWORD = "YOUR_PASSWORD";    // your WiFi password

static const char *MAIL_SERVER = "192.168.1.50";  // the IP from Part 1, step 6
static const uint16_t MAIL_PORT = 25;             // leave as 25
static const char *MAIL_FROM = "esp32@rpi5.local";     // any address; who it is "from"
static const char *MAIL_TO   = "pi@raspberrypi.local"; // your mailbox from step 6
```

Only `MAIL_SERVER` and `MAIL_TO` really matter. `MAIL_FROM` can be anything.

---

## Part 3 - Flash it and watch

Upload the sketch to the ESP32, then open the **Serial Monitor** at **115200** baud (in
the Arduino IDE: Tools -> Serial Monitor). When the board restarts you should see:

```
Connecting to WiFi.....
IP: 192.168.1.174
email sent - check the mailbox on your mail server
```

If it says `email sent`, congratulations - your ESP32 just mailed you! 🎉

---

## Part 4 - Read the email on the mail server

Back on the Raspberry Pi terminal, run:

```bash
mail            # opens a simple mailbox reader; press a number to read, 'q' to quit
```

or simply print the raw mailbox file:

```bash
cat /var/mail/$(whoami)
```

You should see your message, "Hello from your ESP32."

---

## Troubleshooting

The Serial Monitor prints `email failed (SmtpResult N)`. Find `N` here:

| N   | Name                | What it means / how to fix                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| --- | ------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| -2  | `SMTP_ERR_CONNECT`  | The ESP32 could not reach the server. Two very common causes: (a) your WiFi router **isolates wireless devices** from each other ("AP/client isolation") so the ESP32 can't see your LAN server - turn that off, or use a server the ESP32 can reach; (b) your internet provider **blocks outgoing port 25** (almost all do, to fight spam) - that only affects servers on the internet, not your own LAN Postfix, so a LAN server on port 25 is the easy path. For an internet provider, use encrypted submission instead (port 465 with `tls = true`, see "Going further"). Also double-check the `MAIL_SERVER` address. |
| -3  | `SMTP_ERR_TLS`      | Only for encrypted mode. You set `tls = true` but the server does not do TLS on that port. Use port 25 with `tls = false` for a LAN Postfix.                                                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| -4  | `SMTP_ERR_IO`       | The server went quiet. Confirm Postfix is running (`sudo systemctl status postfix`).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| -5  | `SMTP_ERR_PROTOCOL` | The server refused a step. Usually the recipient is not local: make sure `MAIL_TO`'s part after `@` is a name your server owns (`postconf mydestination` lists them).                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| -6  | `SMTP_ERR_AUTH`     | Wrong username/password (only when you set `user`/`pass`).                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| -1  | `SMTP_ERR_ARG`      | A blank `MAIL_SERVER`, `MAIL_FROM`, or `MAIL_TO`.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          |

Still stuck? Watch the mail server's own log while the ESP32 tries:
`sudo tail -f /var/log/mail.log` - it explains, in English, why it accepted or refused.

---

## Going further

- **Send when something happens.** Move the `send_alert(...)` call out of `setup()` and
  into your own code - e.g. inside an `if (temperature > 30)` check - so the board mails
  you only on a real event. Keep the messages short; this is for alerts, not newsletters.

- **Log in to the mail server (AUTH).** If your server requires a username and password,
  set `cfg.user` and `cfg.pass`. The client uses **AUTH LOGIN** automatically.

- **Encrypted mail (SMTPS), e.g. a real provider.** Set `cfg.tls = true` and
  `cfg.port = 465`, and build with TLS on: add `-DPC_ENABLE_TLS=1` next to
  `-DPC_ENABLE_SMTP=1`. The whole conversation is then encrypted.

- **Send a text message (SMS).** Most phone carriers accept an "email-to-SMS gateway"
  address. Put that address in `MAIL_TO` (e.g. `5551234567@txt.att.net` for AT&T in the
  US - search the web for `email to SMS gateway` plus your carrier's name) and your email
  arrives as a text.

---

## Build and run (PlatformIO)

SMTP lives inside the library, so the flag must reach the whole build:

```bash
pio ci examples/L7-Application/SmtpAlert \
  --board esp32dev \
  --lib "." \
  --project-option="build_flags=-DPC_ENABLE_SMTP=1"
```

(The Arduino IDE reads the flag from `build_opt.h` beside the sketch automatically.)

---

## How it works under the hood (for the curious)

`smtp_send()` opens one TCP connection through the library's shared client transport
(`pc_client`) and runs the RFC 5321 conversation: read the `220` greeting, say `EHLO`,
optionally `AUTH LOGIN` (username/password base64-encoded), then `MAIL FROM`, `RCPT TO`,
`DATA`, the message itself, and `QUIT`. The body is **dot-stuffed** (a line that begins
with a `.` gets an extra `.`) so it can never accidentally end the message early, and the
message finishes with the required `<CR><LF>.<CR><LF>`. There is **no heap allocation** -
every buffer is a fixed compile-time size (`PC_SMTP_*`). The conversation logic
(`smtp_run`) is separated from the network behind a tiny send/recv interface, which is why
it can be fully unit-tested on a PC with a pretend server (see `test/test_smtp`).
