# Learn: how networking (and this library) actually works

Welcome! This folder is a from-scratch on-ramp. **It assumes you know nothing about
networking** and builds up from the very beginning. If you have never heard of the
"OSI model", a "packet", or a "port", you are in exactly the right place.

Why a web-server library ships a tutorial: this library is built as a stack of
**layers** that mirror the textbook model of how computers talk to each other. So as
you learn the theory, you can open the matching folder of real, working code and see
the idea in action. The concept and the code line up one-to-one.

## Who this is for

- **Students** meeting networking for the first time.
- **Educators** who want a concrete, readable codebase to teach with.
- **Makers / hobbyists** who can blink an LED and now want their gadget on the web.
- **Working engineers** who want the mental model to click.

No age or background assumed. Where we use a new word, we define it the first time.

## The big idea in one sentence

When two computers talk, the message is handled by a **stack of layers** - each layer
does one small job and hands the result to the layer below it (on the way out) or
above it (on the way in), like passing a letter down through a mailroom, the postal
system, and the delivery truck, then back up at the other end.

## Suggested reading order

1. **[The OSI model](osi-model.md)** - the 7-layer map of how a message travels from
   one program to another. The foundation for everything else.
2. **[TCP/IP: the model the internet actually uses](tcp-ip.md)** - the practical
   4-layer version of the same idea, plus IP addresses, ports, TCP vs UDP, and what
   "a connection" really is.
3. **[A primer on every language in this project](languages.md)** - what C++, HTML,
   CSS, SVG, Python, Markdown (and the config formats) each are, a little history of
   each, and why this project uses the ones it does (and skips Java / JavaScript).

More primers will be added here over time (HTTP, sockets, TLS/encryption, ...). Each
one ties back to the code so you can see the theory running on real hardware.

## How the theory maps to this codebase

The library's network code lives in [`src/network_drivers/`](../../src/network_drivers/),
split into one folder per layer - the same layers you will read about:

```
src/network_drivers/
  physical/      Layer 1 - the actual radio / wire (Wi-Fi, Ethernet)
  datalink/      Layer 2 - framing for the local link
  network/       Layer 3 - IP addresses, getting across networks
  transport/     Layer 4 - reliable streams + ports (TCP/UDP)
  tls/           encryption that wraps the layers above
  session/       Layer 5 - who does the work, and when
  presentation/  Layer 6 - turning raw bytes into requests (HTTP, WebSocket, ...)
  application/   Layer 7 - your routes and handlers (the web server itself)
```

Read a primer, then open the matching folder. That back-and-forth between "the idea"
and "the running code" is the fastest way to truly understand networking.

> A note on style: these `learn/` documents are deliberately wordy and explain
> everything. The **code** itself is the opposite - terse and dense, the way
> production embedded C++ is written. Learning to read both is part of the journey.
