# The OSI model: the 7-layer map of a network

> New here? Start with the [learn index](README.md). This page assumes no prior
> networking knowledge.

When you load a web page, an astonishing amount happens between "click" and "page
appears". To keep it understandable, engineers split the work into **layers**. Each
layer has one job, and only talks to the layer directly above and below it. This
layered map is called the **OSI model** (Open Systems Interconnection) - seven layers,
numbered 1 (closest to the hardware) to 7 (closest to you).

You do not need to memorize it. You need the **intuition**: a message is built up
layer by layer as it leaves one computer, and taken apart layer by layer as it
arrives at the other.

## An analogy: sending a physical letter

Imagine mailing a birthday card to a friend in another country:

1. **You write the card** (the actual message - this is the top layer).
2. **You put it in an envelope** and write the address.
3. **Your local post office** sorts it and decides which city it goes to.
4. **The national postal service** routes it across the country / world.
5. **A truck, plane, and mail carrier** physically move it (the bottom layer - real
   atoms moving through space).

At the other end, the same steps happen **in reverse**: the carrier delivers it, your
friend takes it out of the envelope, and reads the card. Each step only cares about
its own job. The mail carrier does not read your card; you do not drive the truck.

Networking works exactly like this. Each layer **wraps** the data from the layer
above (adds its own "envelope", called a **header**) and hands it down. This wrapping
is called **encapsulation**.

## The seven layers (top to bottom)

We will go top-down, because that is the order data is created when you _send_.

### Layer 7 - Application

The program you actually use, and the rules it speaks. A web browser speaks **HTTP**;
an email app speaks SMTP. This layer is about _meaning_: "GET me the page `/index.html`".

In this library, Layer 7 is the web server you write - your routes and handlers.
Code: [`src/network_drivers/application/`](../../src/network_drivers/application/) and
the public API in [`protocore.h`](../../src/protocore.h).

### Layer 6 - Presentation

Turns the application's _meaning_ into a precise sequence of **bytes** on the wire, and
back again - the "grammar" of the conversation. Parsing an HTTP request's text into
fields, framing a WebSocket message, encoding JSON: all Layer 6.

Code: [`src/network_drivers/presentation/`](../../src/network_drivers/presentation/)
(the HTTP parser, WebSocket framing, SSE, etc.).

### Layer 5 - Session

Manages the _conversation_ itself: who is talking, starting and ending exchanges, and
deciding _when_ work happens. In this library that is the worker task that picks up
incoming data and runs the right handler.

Code: [`src/network_drivers/session/`](../../src/network_drivers/session/).

### Layer 4 - Transport

Delivers a **reliable stream of bytes** between two specific programs. Two key ideas
live here:

- **Ports**: a single computer runs many programs. A **port number** says _which_
  program a message is for (a web server usually listens on port 80, or 443 for the
  encrypted version). Think of it as the apartment number after the street address.
- **TCP vs UDP**: **TCP** guarantees every byte arrives, in order, with nothing lost
  (like a phone call where you confirm you heard each word). **UDP** just fires
  messages off with no guarantee (like shouting across a room - fast, but some words
  may be missed). We cover this in detail in [TCP/IP](tcp-ip.md).

Code: [`src/network_drivers/transport/`](../../src/network_drivers/transport/) - this
is where the library owns all socket I/O.

### Layer 3 - Network

Gets a message **across networks** - from your house to a server on the other side of
the planet, hopping through many routers along the way. The hero here is the **IP
address** (like `192.168.1.85`), the unique-ish number that identifies a machine on
the network. Routing is "which direction do I forward this to get it closer?".

Code: [`src/network_drivers/network/`](../../src/network_drivers/network/) (IPv4,
choosing which interface - e.g. Wi-Fi station vs access point - a message goes out).

### Layer 2 - Data Link

Moves data between two devices on the **same local link** (e.g. your laptop and your
home router) and wraps it in a **frame**. It deals with hardware addresses (**MAC
addresses**) and detecting garbled data on that one hop.

Code: [`src/network_drivers/datalink/`](../../src/network_drivers/datalink/).

### Layer 1 - Physical

The actual physical thing: radio waves for Wi-Fi, electrical pulses in an Ethernet
cable, light in a fiber. Pure 1s and 0s becoming real-world signals.

Code: [`src/network_drivers/physical/`](../../src/network_drivers/physical/) (bringing
the Wi-Fi / Ethernet hardware up and tracking whether the link is alive).

## How data flows: down one side, up the other

Here is the whole journey of one web request, from a browser to this server and back.
Notice how each layer adds a header going **down**, and removes it going **up**:

```
   BROWSER (sending)                         ESP32 SERVER (receiving)
   ----------------                          ------------------------
7  Application   "GET /temperature"      7   Application   run the /temperature handler
6  Presentation  format as HTTP bytes    6   Presentation  parse the HTTP bytes  ^
5  Session       start the exchange      5   Session       hand to a worker      |
4  Transport     [TCP header][ data ]    4   Transport     reassemble the stream |
3  Network       [IP header][  ...   ]   3   Network       check it is for us    | UP
2  Data Link     [frame][    ...      ]  2   Data Link     check this hop        |
1  Physical      radio waves  ~~~~~~~>   1   Physical      radio waves received  |
        |                                                          ^
        +------------- through routers across the internet --------+
            DOWN                                              (then back up)
```

Going down, the message gets wrapped in more and more headers (envelopes). Going up,
each layer reads and strips off _its_ header, then hands the rest upward. The browser's
Layer 4 talks "logically" to the server's Layer 4, even though physically everything
travels all the way down to Layer 1 and back up. That illusion - each layer chatting
with its twin on the other machine - is the heart of the OSI model.

## OSI vs reality

The OSI model is the **teaching map**. The internet itself runs on a simpler,
4-layer cousin called **TCP/IP**, which merges some OSI layers together. They describe
the same reality at different zoom levels. Once the 7 layers make sense, read
**[TCP/IP](tcp-ip.md)** to see the version your devices actually use - and why this
library still keeps the layers cleanly separated.

## Why this library is built in layers

Most embedded web servers blend all of this together. This one keeps each layer in its
own folder with a clean boundary, on purpose:

- You can **read** one layer without understanding the others.
- A bug usually lives in exactly one layer, so it is easier to find and fix.
- You can **swap** a layer (say, Wi-Fi for Ethernet at Layer 1) without touching the
  rest.
- And - for learners - the textbook diagram above _is_ the source tree.

Next: **[TCP/IP: the model the internet actually uses](tcp-ip.md)**.
