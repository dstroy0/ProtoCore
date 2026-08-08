# reverse_engineering - a high-speed DAQ capture + signal-analysis suite

An ESP32 DAQ node that pulls a triggered (or free-running) high-rate capture from an
external instrument, streams it over WiFi in a compact self-describing binary frame, and a
Python engine that turns the result into leakage/attack analytics across four genuinely
different attack families: the _statistical_ ones that need many aligned traces (TVLA
leakage detection, SNR-based points-of-interest, streaming Correlation Power Analysis against
an AES-128 target), the _structural_ one that needs only ONE (Simple Power Analysis:
decoding a secret directly from a trace's own shape - including, deliberately, from where a
trace goes quiet. Silence has a pattern too, and the absence of an operation's characteristic
burst can be exactly as informative as its presence), the _profiled_ one that trades an
expensive one-time characterization pass for needing only a handful of live traces afterward
(template attacks), and the one that needs no probe, amplifier, or ADC at all - just a
stopwatch and a socket (timing side channels). See "Exotic technique: single-trace SPA"
below - it is the one most worth learning early, because it is the attack that makes
constant-time code non-optional; the timing section further down makes the same case from a
completely different angle, with no analog signal involved anywhere.

The same probe -> amplifier -> DAQ -> windowed-capture -> network pipeline applies to side-
channel analysis (below), non-destructive testing (ultrasonic / eddy-current / acoustic-
emission transient capture), and general hardware reverse engineering (bus or signal capture
framed around an external trigger). Nothing before the Python side's AES hypothesis stage is
SCA-specific.

**Authorized use only.** Side-channel analysis of a device you do not own or do not have
explicit written authorization to test is not something this project condones. This suite is
built for security research, hardware evaluation labs, CTF/educational use, legitimate
hardware research (interoperability and clean-room competitive engineering - characterizing a
part's own physical behavior, not reproducing anyone's protected implementation), and
authorized red-team work, all on hardware you own or are authorized to assess - the same
posture as published academic SCA tooling (ChipWhisperer, Riscure Inspector) and the
TVLA/ISO-17825 leakage-assessment methodology it implements.

A deliberate design goal is being a **low-cost teaching platform for a school cyber range**:
an ESP32 plus an off-the-shelf ADC front end (or just a lab bench scope you already have) is a
fraction of the cost of a commercial SCA rig, and running it against intentionally weak or
unprotected crypto (a bare AES implementation with no masking, no hiding) is a concrete way to
teach _why_ constant-time and masked implementations exist - students see a real key fall out
of a real power/EM trace instead of taking the vulnerability on faith. That is the same spirit
as the rest of this library (say hi to Squirty in the root docs): approachable enough for a
motivated kid to get a real result on real hardware, rigorous enough to be exactly the tool a
working security engineer reaches for later. The field needs more people, and the best way in
is a cheap board, a real trace, and a key that visibly falls out of it.

## Architecture

```
NF/EM probe or          conditioning        ADC / oscilloscope       ESP32 DAQ node        WiFi        Python engine
current shunt      -->  amplifier      -->  (the three front     -->  (this firmware)  -->        -->  (dpa_cpa_*.py)
                         (AC-coupled,         ends below)
                          fixed or VGA)
```

Three selectable firmware front ends, one wire protocol (`include/daq_protocol.h`) - see
`src/main.cpp`'s top comment for the full picture:

- **`daq_scpi_scope`** (default). The realistic case for most benches: the "high speed DAQ"
  is a real oscilloscope, and its output is whatever `:WAVeform:DATA?` hands back over SCPI -
  with or without the scope's own trigger armed. The firmware is a SCPI **controller**: it
  opens a TCP connection to the scope's SCPI-RAW socket (port 5025, nearly universal across
  modern benches) using the library's `network_drivers/transport` client and drives it with
  `services/scpi`'s codec - both already shipped in this repository for exactly this "device
  drives a bench instrument" role. No local ring buffer is needed: the scope already did its
  own pre/post-trigger acquisition.
- **`daq_adc_dma`**. The bare-metal case for a bench with no scope: an external ADC (AD9226 -
  12-bit/65 MSPS single, or AD9238 - 12-bit/20-65 MSPS dual; see `adc_profiles.h` for others)
  behind an FPGA/CPLD burst-capture front end drains a triggered burst into the node over SPI
  or UART DMA (`services/dma`), handed off ISR-safe through the preempting work queue's DMA
  lane (`services/preempt_queue`) to `services/trace_capture`, which holds an arbitrary
  millisecond-sized pre-trigger ring and flushes the whole pre+post window the instant a
  trigger lands. AD9238's SPI _configuration_ port (power-down, output format, test pattern -
  not the sample data path, which is parallel-only and out of an MCU's reach at these rates)
  is brought up over the real Arduino SPI library via `services/ad9238`, with a write/
  read-back self-test at boot.
- **`daq_adc_audio`**. No external ADC chip at all: the ESP32's own built-in ADC, polled via
  `analogRead()` off a $1-2 piezo disc or electret mic (`AUDIO_ADC_PIN`, default GPIO34;
  `adc_profiles.h`'s `DAQ_ADC_INTERNAL_AUDIO` profile), still feeding the same
  `services/trace_capture` pre/post-trigger window assembler as the DMA front end above - just
  fed one sample at a time by a paced polling task instead of a DMA burst, since there is no
  external peripheral to drain. A rolling-RMS envelope over the last `AUDIO_RMS_WINDOW` samples
  arms the trigger the instant the input rises `AUDIO_TRIGGER_MARGIN_CODES` above a slowly-
  tracked ambient floor, so "capture what happened when you made a sound" needs no external
  trigger wiring at all. Audio-band acoustic leakage (coil whine, capacitor/relay clicks) is a
  real, published side channel: Genkin, Shamir & Tromer, _RSA Key Extraction via Low-Bandwidth
  Acoustic Cryptanalysis_ (CRYPTO 2014).

Build either with PlatformIO from this directory:

```sh
DAQ_WIFI_SSID='your-ssid' DAQ_WIFI_PASS='your-pass' \
  DAQ_SCOPE_HOST='192.168.1.60' DAQ_ANALYSIS_HOST='192.168.1.50' \
  pio run -e daq_scpi_scope -t upload --upload-port COM7

# free-run variant - no scope trigger armed ("the output of an oscope ... without triggering")
pio run -e daq_scpi_scope_freerun -t upload --upload-port COM7

# bare-metal ADC front end (AD9238 by default; -DDAQ_ADC_PROFILE=DAQ_ADC_AD9226 for the single-
# channel, no-SPI part)
DAQ_WIFI_SSID='your-ssid' DAQ_WIFI_PASS='your-pass' DAQ_ANALYSIS_HOST='192.168.1.50' \
  pio run -e daq_adc_dma -t upload --upload-port COM7

# audio-band acoustic front end - the ESP32's own built-in ADC, no external chip needed
DAQ_WIFI_SSID='your-ssid' DAQ_WIFI_PASS='your-pass' DAQ_ANALYSIS_HOST='192.168.1.50' \
  pio run -e daq_adc_audio -t upload --upload-port COM7
```

`daq_adc_dma` runs on `services/dma`'s shipped ingress/egress **simulator**
(`PC_DMA_SIMULATE=1`, the default) until a real `pc_dma_hw_*` SPI/UART backend is written
_and hardware-verified_ - shipping an unverified driver is against this project's
test-on-hardware rule (see `docs/KNOWN_LIMITATIONS.md`). The DMA -> preempt-queue -> trace-
capture -> network pipeline above it is real and runs identically once that backend lands;
nothing in this firmware changes.

### Analog front end

```
NF/EM probe or shunt --> conditioning amp (fixed or VGA gain, AC-coupled) --> ADC / scope input
```

AC-couple the amplifier output so the ADC's input range holds the signal's swing around 0 V
instead of a DC bias eating into full scale. If the amp is a digitally-controlled VGA (ADI's
AD8331/AD8332 is the common pairing with an AD9226/AD9238 front end in reference designs),
wire its gain control into `main.cpp`'s `daq_set_frontend_gain_db()` weak hook and fold the
applied gain into `FRONTEND_GAIN_LINEAR` so the network engine sees physical volts at a known
gain, not raw codes at an unknown one.

## Wire protocol

`include/daq_protocol.h` / `dpa_cpa_network_engine.py`'s `HEADER_FORMAT` (kept in exact sync -
`struct.calcsize(HEADER_FORMAT) == 56` is asserted at import; `DAQ_PROTO_VERSION 2`). One
packet: `[56-byte header][payload_len bytes of samples][uint16 payload CRC16]`. Binary,
memcpy-framed, no text formatting anywhere in the per-window hot path - see the header's own
doc comment for why that matters at DMA-chunk rates.

| Field                | Type      | Meaning                                                                                    |
| -------------------- | --------- | ------------------------------------------------------------------------------------------ |
| `magic`              | `char[4]` | `"DAQ1"`                                                                                   |
| `version`            | `u16`     | `DAQ_PROTO_VERSION`                                                                        |
| `msg_type`           | `u8`      | `DAQ_MSG_WINDOW` / `DAQ_MSG_HEARTBEAT`                                                     |
| `frontend`           | `u8`      | `DAQ_FRONTEND_ADC_DMA` / `DAQ_FRONTEND_SCPI_SCOPE`                                         |
| `trace_id`           | `u32`     | monotonic capture sequence                                                                 |
| `n_samples`          | `u32`     | samples in the payload (per channel)                                                       |
| `pretrigger_samples` | `u16`     | how many precede the trigger instant                                                       |
| `sample_bytes`       | `u8`      | 1 or 2                                                                                     |
| `channel_count`      | `u8`      | 1, or 2 for an AD9238 dual capture                                                         |
| `x_increment_s`      | `f32`     | seconds/sample from the source (scope `:WAV:XINC?`); 0 if `sample_rate_hz` applies instead |
| `sample_rate_hz`     | `f32`     | the ADC/DMA front end's fixed rate; 0 if `x_increment_s` applies instead                   |
| `y_increment`        | `f32`     | volts/code                                                                                 |
| `y_origin`           | `f32`     | volts at code 0                                                                            |
| `windows_dropped`    | `u32`     | telemetry since link-up                                                                    |
| `assembly_ns`        | `u32`     | trigger-to-window latency (0 if not applicable)                                            |
| `wall_clock_us`      | `u64`     | Unix epoch microseconds this window's trigger landed at; 0 until a time source syncs       |
| `payload_len`        | `u16`     | bytes following the header                                                                 |
| `header_crc16`       | `u16`     | CRC16/CCITT-FALSE over the header (this field zeroed during calc)                          |

Every window is fully self-describing (sample width, trigger split, physical scaling), so a
SCPI oscilloscope pull and a raw ADC/DMA capture look identical on the wire past the header -
`dpa_cpa_network_engine.py` never hardcodes a front end's assumptions.

`SideChannelStreamReceiver` (`dpa_cpa_network_engine.py`) resyncs on the magic word on any
corruption instead of tearing down the link, and independently verifies the header and
payload CRCs. There is **no plaintext field on the wire** - the original mock firmware faked
one (`memset(plaintext, 0xAA, 16)`); the real firmware makes no claim about what the target
processed during a window, because that is not observable from an oscilloscope pull or a raw
ADC burst on their own. Wire a `plaintext_source(trace_id) -> bytes | None` callback into
`SideChannelStreamReceiver` when you have a real correlation source (a debug UART tap, a bus
sniff, or simply that you are the one driving the target's input); TVLA and SNR need no
plaintext at all.

### Wall-clock sync (`wall_clock_us`)

`trace_id` alone only orders windows relative to each other; `wall_clock_us` anchors a window
to a real Unix-epoch timestamp. `main.cpp`'s `wall_clock_us_now()` anchors `pc_micros()`
against `services/ntp_service`'s synced epoch second the first time `pc_ntp_synced()` goes
true, and re-anchors on every epoch-second rollover so `pc_micros()`'s own wraparound never
corrupts it - then adds the sub-second `pc_micros()` delta for microsecond resolution riding
on a whole-second NTP anchor. It reads `0` until the first sync lands, so a receiver can tell
"no time source yet" from a real (if only ordinary-NTP-accurate) timestamp. This is what makes
it possible to correlate multiple DAQ nodes' captures in absolute time - an EM probe node and a
power-rail node capturing the _same_ physical event from two vantage points, or lining a
capture up against an externally logged event time - without needing a shared trigger line
between them. (`services/gnss_survey` in this library is position-surveying only, not a
GNSS-PPS time source, so it is not a path to tighter-than-NTP sync today; ordinary NTP is
typically low-single-digit milliseconds, fine for this correlation use case, not for
sub-microsecond multi-node alignment.)

## Analysis techniques (`dpa_cpa_engine.py`)

- **Windowed cross-correlation trace alignment** - clock-jitter compensation; reliable when a
  common deterministic waveform shape dominates the noise floor (any real captured execution
  trace), not adversarially testable against a bare noise-plus-leak synthetic (see the code
  comments in the smoke tests for why those skip it).
- **Zero-phase (filtfilt) Butterworth bandpass** - strips DC bias and RF harmonics without the
  phase shift a causal filter would reintroduce as apparent jitter.
- **TVLA** (fixed-vs-random Welch's t-test, `|t| > 4.5`) - the ISO/IEC 17825 non-specific
  leakage-detection standard: answers "does this implementation leak at all" without assuming
  an AES Hamming model, so it is the right first pass before spending traces on a targeted
  CPA. Goodwill et al., _A testing methodology for side-channel resistance validation_ (NIST
  NIAT 2011); recent work rethinking TVLA for post-quantum targets is a live reminder that a
  clean result is "no leakage **found**", not "no leakage" (Keysight, _Rethinking TVLA for
  PQC_, 2025).
- **SNR** per sample point - the standard points-of-interest metric, and the pre-filter step
  ahead of a deep-learning SCA model in the current literature (2025-2026 CNN/Transformer
  attacks reduce to the highest-SNR few hundred samples before training).
- **Streaming (incremental) CPA** - running sums of x, x², y, y², xy update per batch; the
  correlation matrix is a closed-form read of those sums (Le Bouder et al., _Computational
  Aspects of Correlation Power Analysis_, IACR ePrint 2015/260). No full trace matrix is ever
  held, so this scales to however long the DAQ link runs - the actual constraint a "chunking
  from a high speed DAQ" firmware has to satisfy.
- **Full AES-128 key recovery** - independent Hamming-distance CPA per SubBytes byte, all 16.
- **Second-order CPA** (centered-product combining over a candidate points-of-interest pair)
  for first-order-masked targets - masking is the standard algorithmic countermeasure, and a
  first-order CPA alone reads as noise against it. See the Ledger Donjon writeups on
  second-order attacks against masked Kyber/ML-KEM (2025) for the modern combiner context;
  this module's version targets a masked AES SubBytes, the DPA-classic case.

## Exotic technique: single-trace SPA (`spa_engine.py`)

Everything above is _statistical_: it asks whether a sample point correlates with a
hypothesis (or differs between two classes) once averaged over hundreds of traces. Simple
Power Analysis asks a structurally different question of a **single** trace: does the trace's
own shape - its sequence of active bursts and idle gaps - encode the secret directly? This is
the attack Kocher's original 1996 DPA paper describes first, before it gets to the statistical
part, and it is why constant-time code is not optional: if a crypto implementation's control
flow depends on secret data (the textbook case is RSA/ECC square-and-multiply, where a
"multiply" step runs only for a 1-bit), the presence or **absence** of that step's
characteristic burst _is_ the secret bit, sitting there in one trace with no averaging and no
key hypothesis needed. Silence is not the absence of information - a _patterned_ silence is
data.

- **Activity-envelope segmentation** - a moving-energy envelope thresholded via Otsu's method
  (Otsu, _A Threshold Selection Method from Gray-Level Histograms_, IEEE SMC 1979 - a 1-D
  image-processing technique applied here to a power/EM envelope instead of pixel intensities)
  into alternating burst/gap runs, with no hand-picked magic threshold.
- **Periodic burst-count decoding** - the square-and-multiply case: count bursts per period
  (autocorrelation-estimated if not already known). One burst = a 0 bit; two or more = a 1 bit
    - the extra burst's _absence_ is read as directly as its presence.
- **Zero-value scan** - flags bursts with anomalously low energy relative to the population:
  multiplying or adding by zero characteristically switches fewer bits than a random operand,
  and stands out without knowing which operation it was. See Akishita & Takagi, _Zero-Value
  Point Attacks on Elliptic Curve Cryptosystem_ (ISC 2003), for the ECC case this generalizes
  from.

`tests/simulate_pipeline.py` sends one synthetic square-and-multiply trace over the _same_
wire protocol as the CPA batch and decodes its 12-bit bit pattern alone, off one window - see
it run end to end alongside the statistical techniques above.

## Template attacks (`template_attack_engine.py`)

CPA and TVLA are both _unprofiled_: they know a leakage model (Hamming distance) but nothing
about the specific device's leakage shape. A **template attack** characterizes that shape
first, then needs far fewer traces to attack with it afterward - the standard worst-case SCA
evaluation model (an evaluator profiles on a clone device with a known key, the way a real
assessment would use a purchased or otherwise available reference unit). Chari, Rao, Rohatgi,
_Template Attacks_ (CHES 2002); this module's pooled-covariance simplification (one shared
covariance matrix across classes, only the per-class means differ) follows Choudary & Kuhn,
_Efficient Template Attacks_ (CARDIS 2013), which stays numerically stable with a far more
modest profiling set than a full per-class covariance needs.

- **Profile** (`TemplateAttack.profile`) - build a Gaussian template (mean + pooled covariance)
  per Hamming-weight class (0-8) of the leaking intermediate, at a handful of SNR-selected
  points of interest (`select_poi`, reusing `dpa_cpa_engine.signal_to_noise_ratio`).
- **Attack** (`TemplateAttack.attack_key_byte`) - score an unknown trace against every class by
  Mahalanobis distance, then sum each of the 256 key hypotheses' predicted-class
  log-likelihood across a handful of attack traces; the hypothesis that best explains what was
  actually observed wins. The demo profiles on 3000 known-key traces from a clone device, then
  recovers the target's own (unknown, different) key byte from as few as 5-20 attack traces.

    python template_attack_engine.py

## Timing side channel (`timing_engine.py`)

The cheapest entry point in this entire suite: no probe, amplifier, ADC, or scope - just a
stopwatch and a TCP socket. A naive secret comparison that returns the instant it finds a
mismatched byte leaks how much of a guess's prefix was correct through how long it took to
answer - Kocher's original 1996 timing-attacks paper, made remotely practical over a real
network by Brumley & Boneh, _Remote Timing Attacks Are Practical_ (USENIX Security 2003).

- **`VulnerableCompareServer`** - early-exit byte-by-byte compare. `recover_secret_via_timing`
  guesses one byte at a time, measuring every candidate's response latency as a min-of-N-trials
  per candidate (jitter only ever adds delay, so the minimum across trials is the least-biased
  estimate of the true cost - Crosby, Wallach & Riedi, _Opportunities and Limits of Remote
  Timing Attacks_, 2009) and taking a majority vote across several independent full-alphabet
  scans per byte position for extra margin against a stray scheduling spike.
- **`ConstantTimeCompareServer`** - the fix: walk every byte unconditionally and combine with
  OR-of-XOR rather than branching on the result, so total execution time depends only on the
  guess's length, never its content (`services/jwt`'s `jwt_verify_hs256` in this library's C++
  side already does the same for exactly this reason). `position_latency_spread` gives a single
  number - max-min latency across every candidate byte at a position - that collapses by an
  order of magnitude or more once the fix is in, a clean before/after readout of the leak.

    python timing_engine.py

## Vulnerable vs. hardened: countermeasures that change the outcome (`hardening_demo.py`)

Two before/after stories, built entirely on the analysis primitives already above - the payoff
the rest of the toolbox builds toward: not just "here is an attack" but "here is what a
specific implementation choice actually does to it," with both directions run and asserted.

- **Masking vs. CPA order.** A first-order Boolean-masked target (`intermediate = share1 ^
mask`, a fresh uniform-random `mask` every trace) defeats a first-order CPA outright -
  individually, neither share's Hamming weight correlates with the real intermediate; that IS
  the security property masking buys. `compute_second_order_cpa`'s centered-product combiner
  over both share positions recovers the key anyway (Messerges, _Using Second-Order Power
  Analysis to Attack DPA Resistant Software_, CHES 2000): masking raises the bar, it does not
  remove the leakage.
- **Desync vs. alignment.** A randomly time-jittered leak (a "hiding" countermeasure, cheaper
  than masking) defeats a naive fixed-sample-position CPA outright - the correlation smears
  across however many sample positions the jitter can land on. Windowed cross-correlation
  realignment (`align_trace_jitter`) against a strong shared reference feature undoes a simple
  single-shift-per-trace jitter and recovers the key anyway - real hiding countermeasures need
  jitter deep or irregular enough to survive this, which is the point of the story, not that
  hiding is worthless.

    python hardening_demo.py

## Target firmware: vulnerable vs. hardened (`targets/leaky_target/`)

Everything above is capture and analysis tooling - it needs something real to point a probe at.
`targets/leaky_target/` is a physically-probeable target: a single AES SubBytes step
(`PC_AES_SBOX[plaintext ^ KEY_BYTE] ^ plaintext`, the exact Hamming-distance intermediate this
whole suite's CPA/TVLA/SPA/template code already targets) running on real silicon, with
`TRIGGER_PIN` (default GPIO4) toggling around it so a capture front end has a clean edge to arm
on. Two build environments, one source file:

- **`vulnerable`** - the op runs at a fixed time after the trigger edge, every iteration. A CPA
  at that fixed sample offset recovers `KEY_BYTE` cleanly.
- **`hardened`** - the same op, preceded by a random `0`-`DESYNC_WINDOW_US` delay drawn fresh
  every iteration - `hardening_demo.py`'s Story 2 desync countermeasure, running on real
  hardware instead of a synthetic trace. Defeats a naive fixed-position CPA unless the desync
  window is shallow enough for realignment to undo it.

```sh
cd targets/leaky_target
pio run -e vulnerable -t upload --upload-port COM7   # or -e hardened
```

Tail the target's own serial log (`PT <iteration> <plaintext_hex>`) alongside a DAQ node's
capture log to correlate a captured `trace_id` with the plaintext that produced it - the same
`plaintext_source(trace_id) -> bytes | None` hook the wire protocol section above describes.

## Setup

```sh
pip install numpy scipy

# start the network engine (listens on :8080 by default)
python dpa_cpa_network_engine.py

# standalone single-trace SPA demo (no network, no firmware needed) - burst/gap segmentation,
# periodic bit decoding, and a zero-value scan against a synthetic square-and-multiply trace
python spa_engine.py

# standalone template attack demo - profile on a known-key clone, recover an unknown key
# byte from a handful of attack traces
python template_attack_engine.py

# standalone timing side-channel demo - recovers a secret over plain TCP timing alone, then
# shows the constant-time fix collapsing the measurable signal
python timing_engine.py

# standalone hardening demo - masking defeats CPA / 2nd-order CPA defeats masking;
# desync defeats CPA / realignment defeats desync
python hardening_demo.py

# end-to-end verification: synthesizes DaqPacketHeader-framed AES-128 traces AND a single SPA
# trace, ships them over a real loopback TCP socket, and runs alignment/filter/TVLA/SNR/
# streaming-CPA/full-key-recovery/SPA against the result - the wire protocol's framing/CRC/
# resync code paths included.
python -m tests.simulate_pipeline
```

## New library primitives this work added (`src/`)

- **`services/trace_capture`** (`PC_ENABLE_TRACE_CAPTURE`) - the pre/post-trigger window
  assembler: a continuously-running pre-trigger ring, `pc_tc_trigger()` freezing it as the
  window's pre-trigger half, `pc_tc_feed()` filling the post-trigger half and firing the sink
  on completion. Zero-heap, fail-closed (one capture in flight), ISR-safe. Host-tested
  (`test/test_trace_capture`).
- **`services/ad9238`** (`PC_ENABLE_AD9238`) - the AD9238's SPI configuration-port framing
  (the 16-bit instruction word + shadow-register transfer shared across this generation of ADI
  high-speed ADCs). Host-tested (`test/test_ad9238`); the per-register bit-field constants are
  transcribed from the datasheet and, per this project's hardware-verification policy, not yet
  confirmed against physical silicon - see `services/peripherals/ad9238/ad9238.h`'s confidence note before
  writing to a real part.
- **`services/clock.h`: `pc_cycles()` / `pc_cycles_to_ns()`** - a CPU-cycle-counter time
  source for sub-microsecond jitter measurement, alongside the existing millisecond/microsecond
  tiers. `pc_micros()` under-resolves a single SPI-DMA transaction (one byte at 20 MHz SPI is
  400 ns); `trace_capture` uses this to report `assembly_ns` (trigger-to-window latency).
- **`services/dma`: `pc_dma_event::t_us`** - a microsecond completion timestamp alongside the
  existing `t_ms`, for peripherals fast enough to complete several transfers inside one
  millisecond tick.

## Known limitations

- `daq_adc_dma`'s bulk sample ingest runs on the shipped DMA simulator until a real
  `pc_dma_hw_*` SPI/UART backend is written and bench-verified (see `docs/KNOWN_LIMITATIONS.md`
    - this is a whole-library policy, not specific to this firmware).
- `services/ad9238`'s per-register bit fields need confirmation against your part's exact
  datasheet revision before a real SPI write (see the confidence note in `ad9238.h`).
- `adc_profiles.h`'s `DAQ_ADC_GENERIC` profile is a conservative placeholder - fill in your
  part's real resolution / max rate / pipeline latency rather than shipping traces with the
  wrong scaling.
- The SCPI command macros in `main.cpp` (`SCPI_WAVE_*`) match the `:WAVeform:...` subsystem
  naming common to Keysight/Rigol/Siglent benches; a Tektronix uses `WFMOutpre:` / `CURVe?`
  instead - swap the macros for your instrument.
- `daq_adc_audio`'s capture quality depends on the ESP32's built-in ADC, which is well known to
  be noisier and less linear than a dedicated external ADC (AD9226/AD9238) - fine for the
  acoustic-cryptanalysis teaching demo this profile targets, not a substitute for a real DAQ
  front end in a rigorous evaluation.
- `wall_clock_us` is only as accurate as `services/ntp_service`'s ordinary NTP sync (typically
  low-single-digit milliseconds), not a GNSS-PPS-disciplined clock - see "Wall-clock sync"
  above for what it is and is not good for.
