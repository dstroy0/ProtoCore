# A primer on every language in this project

> Part of the [learn series](README.md). No programming background assumed.

A real software project is rarely written in just one language. Different jobs call for
different tools: one language runs on the tiny microcontroller, another builds the
documentation, another describes a web page. This page is a quick, friendly tour of
**every language and file format actually used in this repository**, with a little
history for each and a note on _why it is here_.

First, two words you will see a lot:

- **Compiled** language: you write text, then a program called a **compiler** turns it
  into raw machine instructions ahead of time. Fast to run, strict about mistakes.
  (C, C++.)
- **Interpreted** / scripting language: another program reads your text and runs it on
  the spot, line by line. Slower, but quick to write and change. (Python, shell.)

And a quick honest map of this repo, by file count:

| Language / format                   | Files            | What it does here                         |
| ----------------------------------- | ---------------- | ----------------------------------------- |
| C++ (`.cpp`, `.h`, `.ino`)          | most of the repo | the library itself + examples             |
| Markdown (`.md`)                    | the docs         | every document, including this one        |
| YAML (`.yml`)                       | CI config        | the automated test/build pipelines        |
| CSS (`.css`)                        | web assets       | styling the served pages + the docs theme |
| HTML (`.html`)                      | web assets       | the structure of served pages             |
| SVG (`.svg`)                        | graphics         | vector images (the dashboard gauges)      |
| Python (`.py`)                      | tooling          | scripts that generate/check project files |
| JSON / TOML / INI / CFG             | config           | machine-readable settings                 |
| Shell (`.sh`) / PowerShell (`.ps1`) | scripts          | small automation helpers                  |

Notably **not** here: Java and JavaScript. We explain why at the end - their absence is
a deliberate, instructive choice.

---

## The core: C and C++

### C (1972)

**C** was created by Dennis Ritchie at Bell Labs to write the Unix operating system. It
is small, fast, and gives you direct control over the computer's memory - which is
exactly what you need when writing an operating system or talking to hardware. Almost
every language since has borrowed C's syntax (the curly braces `{ }`, the semicolons).
More than 50 years later, C still runs the world's low-level software: OS kernels,
device drivers, and microcontrollers.

The trade-off: C trusts you completely. It will happily let you read past the end of an
array or forget to free memory. Power and danger in the same hand.

### C++ (1985) - what this library is written in

**C++** was created by Bjarne Stroustrup as "C with classes": keep C's speed and
hardware control, but add tools to organize bigger programs - **classes** (bundling
data with the code that acts on it), templates, and stronger type checking. It is still
one of the fastest languages in existence, which is why it dominates games, browsers,
trading systems, and embedded devices.

**Why this library uses C++:** it runs on an **ESP32**, a microcontroller with only a
few hundred kilobytes of memory and no operating system to lean on. C++ lets the code
be both _organized_ (the clean OSI layers you read about) and _tiny and fast enough_ to
fit. This project also follows strict embedded discipline: no heap allocation after
startup, fixed-size buffers, no standard-library bloat - C++ allows that level of
control while still reading nicely.

- The `.cpp` files are the implementation; the `.h` (**header**) files declare what is
  available so other files can use it.
- The `.ino` files are **Arduino sketches** - the example programs. An `.ino` is just
  C++ with a friendlier wrapper that the Arduino toolchain understands (it provides
  `setup()` and `loop()` for you). Every example in [`examples/`](../EXAMPLES.md) is
  an `.ino`.

> The code is written in a deliberately **terse** style (short names, dense lines) -
> that is normal for professional embedded C++. These learning docs are the opposite,
> on purpose. Reading both is a skill worth building.

---

## The web trio: HTML, CSS, and SVG

A web server serves web pages, so the repo includes the three languages a browser
understands. Crucially, the browser is the one computer in this story we do **not**
control - so we speak its native languages.

### HTML (1991) - structure

**HTML** (HyperText Markup Language), invented by Tim Berners-Lee at CERN alongside the
web itself, describes the **structure and content** of a page using **tags**:
`<h1>A heading</h1>`, `<p>a paragraph</p>`. It says _what things are_, not how they
look. It is a **markup** language, not a programming language - there is no logic, just
labeled content.

### CSS (1996) - appearance

**CSS** (Cascading Style Sheets) describes **how the HTML should look** - colors,
fonts, spacing, layout. Separating "what it is" (HTML) from "how it looks" (CSS) means
you can restyle a whole site without touching its content. This repo uses CSS both for
the pages the device serves and for the theme of the generated API documentation.

### SVG (2001) - vector graphics

**SVG** (Scalable Vector Graphics) describes images as **math, not pixels** - "draw a
circle here, a line there". Because it is math, it stays razor-sharp at any size, and
because it is just text, the device can generate it on the fly. This library draws its
dashboard gauges and charts as SVG.

> **Why no JavaScript?** Normally the web's fourth language, **JavaScript**, adds
> interactivity in the browser. This library deliberately avoids it: the dashboard is
> built from hand-written HTML + CSS + SVG with **no JavaScript at all**. On a tiny
> device serving simple control panels, that keeps things small, fast, and dependency-
> free - and it is a neat demonstration of how much you can do without it.

---

## The tooling languages: Python, shell, PowerShell

These never run on the ESP32. They run on a _developer's_ computer to build, test, and
maintain the project.

### Python (1991)

**Python**, created by Guido van Rossum, is famous for being easy to read - it looks
almost like English and uses indentation instead of braces. It is interpreted (no
compile step), which makes it perfect for quick automation. Here it generates and
checks project files - for example [`test/gen_test_envs.py`](../../test/gen_test_envs.py)
builds the long list of test configurations from one small table, so a human never has
to maintain them by hand.

### Shell (`.sh`) and PowerShell (`.ps1`)

**Shell scripts** are the language you type into a terminal, saved to a file so the
computer can repeat the steps. The classic Unix shell dates to the 1970s; **PowerShell**
(2006) is Microsoft's modern, object-oriented take for Windows. This repo has a small
one of each (e.g. a test runner) so the same commands work on Linux/macOS and Windows.

---

## The "languages" that are really data formats

These do not _do_ anything on their own - they **describe** things in a way both humans
and programs can read. People often call them languages loosely.

- **Markdown** (`.md`, 2004) - the language of _these very docs_. It turns plain text
  with a few simple marks (`# heading`, `**bold**`, `- list`) into formatted documents.
  Designed by John Gruber to be readable even before it is rendered. Every file in
  `docs/` is Markdown.
- **JSON** (`.json`, early 2000s) - JavaScript Object Notation: a simple way to write
  structured data as `{"key": "value"}`. Used here for machine-readable config like
  [`library.json`](../../library.json).
- **YAML** (`.yml`) - a more human-friendly data format (indentation-based) used for the
  **CI** pipelines in [`.github/`](../../.github/) - the scripts a server runs
  automatically to build and test every change.
- **TOML / INI / CFG / properties** - assorted small, simple "setting = value" config
  formats used by various tools (the build system, the version bumper, the editor).

---

## "But you said Java?"

Java was on your list, so here is the honest answer: **this library contains no Java**
(and no JavaScript). Both are excellent languages - just not the right fit here.

- **Java** (1995, James Gosling at Sun Microsystems) runs on a **virtual machine** (the
  JVM): your code compiles to portable "bytecode" that the JVM runs on any platform -
  "write once, run anywhere". That portability needs a chunky runtime and a garbage
  collector managing memory for you. Wonderful for servers and Android apps;
  impractical on a microcontroller with kilobytes of RAM and hard real-time needs,
  where this project instead uses C++ with no garbage collector and no heap churn.
- **JavaScript** (1995, Brendan Eich) is the language of interactivity _inside the
  browser_. As noted above, this library purposely serves JS-free pages.

Knowing _why_ a tool is **not** used is as valuable as knowing why one is. The whole
theme of this library - tiny, predictable, no hidden costs - is the reason its language
choices look the way they do.

Next, see these languages arranged as the network stack they implement:
**[the OSI model](osi-model.md)** and **[TCP/IP](tcp-ip.md)**.
