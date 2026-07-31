# TCP/IP: the model the internet actually uses

> This follows on from **[The OSI model](osi-model.md)**. If you have not read that
> yet, read it first - this page builds on the layer idea.

The [OSI model](osi-model.md) has seven layers and is the great _teaching_ map. But the
real internet was built on a more practical model with **four** layers, called
**TCP/IP** (after its two most famous protocols, **TCP** and **IP**). It describes the
same journey - it just groups some of OSI's layers together.

## The four layers, and how they line up with OSI

```
   TCP/IP (real world)        OSI (teaching map)        In this library
   -------------------        ------------------        ---------------
   4. Application       <-->   7 Application             application/
                              6 Presentation            presentation/
                              5 Session                 session/
   3. Transport        <-->   4 Transport               transport/
   2. Internet         <-->   3 Network                 network/
   1. Link             <-->   2 Data Link               datalink/
                              1 Physical                physical/
```

So TCP/IP's **Application** layer rolls OSI's top three into one, and its **Link** layer
rolls OSI's bottom two. Same reality, fewer boxes. (Encryption, **TLS**, does not get
its own number in either model - it tucks in just under the application data. This
library keeps it in its own [`tls/`](../../src/network_drivers/tls/) folder.)

## The two stars: IP and TCP

### IP - getting a packet to the right machine

**IP** (Internet Protocol) is the Internet/Network layer. Its one job: take a chunk of
data, wrap it in a header that says **where it is going** and **where it came from**,
and let the network's **routers** forward it hop by hop toward the destination.

- An **IP address** is the machine's number, like `192.168.1.85` (IPv4: four numbers
  0-255). It is the "street address" of a device.
- A wrapped chunk of data with an IP header is called a **packet**.
- IP is **best-effort**: it _tries_ to deliver each packet, but makes no promise.
  Packets can be lost, duplicated, or arrive out of order. That sounds alarming - which
  is exactly why TCP exists.

### TCP - turning unreliable packets into a reliable stream

**TCP** (Transmission Control Protocol) sits on top of IP (the Transport layer) and
provides what most programs actually want: **a reliable, ordered stream of bytes**.
You put bytes in one end; the exact same bytes come out the other end, in order, with
nothing missing - even though the IP packets underneath were unreliable.

TCP pulls this off with a few tricks:

- **The handshake.** Before any data, the two sides exchange three small messages
  (called SYN, SYN-ACK, ACK) to agree "we are now connected". This is the famous
  **three-way handshake**, and it is what "opening a connection" means.
- **Acknowledgements (ACKs).** The receiver tells the sender "got bytes up to here".
  Anything not acknowledged in time is **re-sent**. That is how loss is hidden.
- **Sequence numbers.** Every byte is numbered, so the receiver can put out-of-order
  packets back in the right order.
- **Flow control.** The receiver advertises how much buffer space it has (the
  **window**) so a fast sender cannot overwhelm a slow receiver. This library's
  transport layer manages exactly this - see the "ack-on-consume" design in
  [ARCHITECTURE.md](../ARCHITECTURE.md); getting it wrong caused a real bug, written up
  in [BUGS.md](../BUGS.md).

### UDP - the fast, no-promises alternative

**UDP** (User Datagram Protocol) is TCP's minimalist sibling, also at the Transport
layer. It just sends a packet (a **datagram**) and forgets about it - no handshake, no
ACKs, no ordering. It can be lost without anyone noticing.

Why would you ever want that? **Speed and simplicity.** For things like a sensor
shouting a reading every second, or DNS lookups, losing one is no big deal and the
overhead of TCP is not worth it. This library uses UDP for exactly those cases (DNS,
telemetry casts, the captive-portal responder).

|                   | TCP                                         | UDP                            |
| ----------------- | ------------------------------------------- | ------------------------------ |
| Reliable delivery | yes (re-sends lost data)                    | no                             |
| Keeps order       | yes                                         | no                             |
| Connection setup  | yes (handshake)                             | no                             |
| Speed / overhead  | more overhead                               | very light                     |
| Good for          | web pages, files, anything that must arrive | live telemetry, DNS, discovery |

### Ports - which program gets the data

One machine (one IP address) runs many network programs at once: a web server, maybe
an SSH login, a sensor feed. A **port number** (0-65535) says _which_ program a message
is for. The IP address finds the **machine**; the port finds the **program** on it.

Some ports are conventional: **80** = HTTP (web), **443** = HTTPS (encrypted web),
**22** = SSH, **53** = DNS. When this server starts with `server.begin(80)`, it is
saying "deliver anything arriving on port 80 to me".

The combination **IP address + port** (e.g. `192.168.1.85:80`) uniquely identifies one
endpoint of a conversation. A TCP connection is really _four_ numbers: your IP+port and
their IP+port.

## Putting it together: what happens when a browser hits this server

Say your ESP32 runs this library at `192.168.1.85` and you open `http://192.168.1.85/`
in a browser:

1. **DNS (maybe).** If you typed a name instead of the number, the browser first asks a
   DNS server (over UDP, port 53) "what is the IP for this name?". Here you typed the
   number, so this is skipped.
2. **TCP handshake.** The browser opens a TCP connection to `192.168.1.85:80` (the
   three-way handshake). Now there is a reliable byte-pipe between the two.
3. **HTTP request.** The browser sends bytes that mean `GET / HTTP/1.1` plus some
   headers (Application layer). TCP carries them reliably; IP routes the packets; Wi-Fi
   moves the bits.
4. **The server works.** On the ESP32, the bytes travel **up** the stack: the physical
   Wi-Fi receives them, the transport layer reassembles the TCP stream, the
   presentation layer parses the HTTP text, and the application layer runs your handler
   for `/`.
5. **HTTP response.** Your handler produces a page; it travels back **down** the stack
   and across to the browser, which renders it.
6. **Close.** The connection is closed (or kept alive for the next request).

Every numbered step maps to a folder in
[`src/network_drivers/`](../../src/network_drivers/). That is the whole point: the
theory you just read _is_ the shape of the code.

## Where to go next

- See the layers as folders: [the architecture map](../ARCHITECTURE.md).
- See the exact specifications behind each protocol: [STANDARDS.md](../STANDARDS.md)
  (for example, TCP is RFC 793, IP is RFC 791, and HTTP/1.1 is RFC 9112).
- Re-read [the OSI model](osi-model.md) now that the four-layer version has clicked -
  the seven-layer version will make more sense the second time.
