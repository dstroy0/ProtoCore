"""TCP receiver for the reverse_engineering firmware's DaqPacketHeader wire protocol
(daq_protocol.h) - resync-safe framing, dual CRC16 verification, physical-unit scaling
(y_increment/y_origin, already correct whether the source is a real scope's :WAV:YINC?/
:WAV:YOR? or the ADC front end's v_ref/full-scale-codes), and a pluggable plaintext
correlation hook for the CPA stage.

## Why there is no plaintext in the packet itself

The original mock firmware stuffed a fake `memset(plaintext, 0xAA, 16)` into every packet -
a placeholder that looked like real data but never was. The real firmware makes no claim
about what the target processed during a capture window, because that is not something an
oscilloscope pull or a raw ADC burst can know on its own: it has to come from wherever you
are actually driving the target (a debug UART tap, a bus sniff, or simply the fact that you
are the one feeding it the plaintext). Wire a `plaintext_source(trace_id) -> bytes | None`
callback into SideChannelStreamReceiver to attach it when you have one; when you don't,
tvla_t_test() / signal_to_noise_ratio() (dpa_cpa_engine.py) need no plaintext at all - TVLA's
fixed-vs-random classification is a property of what you fed the target, not what this
receiver observed, and is the recommended first pass before spending traces on a targeted
CPA in any case.
"""

import socket
import struct
import threading
from dataclasses import dataclass
from queue import Empty, Queue
from typing import Callable, List, Optional, Tuple

import numpy as np

MAGIC = b"DAQ1"
PROTO_VERSION = 2  # v2 added wall_clock_us (48 -> 56-byte header)
MSG_WINDOW = 1
MSG_HEARTBEAT = 2

# Mirrors DaqPacketHeader (daq_protocol.h) byte-for-byte: little-endian, no padding.
HEADER_FORMAT = "<4sHBBIIHBBffffIIQHH"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
assert HEADER_SIZE == 56, f"HEADER_FORMAT drifted from daq_protocol.h (got {HEADER_SIZE} bytes, want 56)"
TRAILER_FORMAT = "<H"
TRAILER_SIZE = struct.calcsize(TRAILER_FORMAT)


def crc16_ccitt_false(data: bytes, crc: int = 0xFFFF) -> int:
    """CRC16/CCITT-FALSE - bit-for-bit the same algorithm as daq_protocol.h's daq_crc16()."""
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc


def _verify_header_crc(raw_header: bytes, claimed_crc: int) -> bool:
    zeroed = raw_header[:-2] + b"\x00\x00"  # header_crc16 is computed with itself zeroed
    return crc16_ccitt_false(zeroed) == claimed_crc


@dataclass
class CapturedWindow:
    trace_id: int
    frontend: int
    n_samples: int
    pretrigger_samples: int
    sample_bytes: int
    channel_count: int
    x_increment_s: float
    sample_rate_hz: float
    y_increment: float
    y_origin: float
    windows_dropped: int
    assembly_ns: int
    wall_clock_us: int  # Unix epoch microseconds, or 0 if the DAQ node had no synced time source
    codes: np.ndarray  # raw ADC/scope codes, shape (n_samples,) or (n_samples, channel_count)
    volts: np.ndarray  # codes * y_increment + y_origin, same shape
    plaintext: Optional[bytes] = None


class SideChannelStreamReceiver:
    """Hosts a TCP socket accepting one DAQ node connection and decoding its DaqPacketHeader
    stream into CapturedWindow objects, queued for gather_batch()."""

    def __init__(
        self,
        host: str = "0.0.0.0",
        port: int = 8080,
        max_payload: int = 1 << 20,
        plaintext_source: Optional[Callable[[int], Optional[bytes]]] = None,
    ):
        self.host = host
        self.port = port
        self.max_payload = max_payload
        self.plaintext_source = plaintext_source
        self.packet_queue: "Queue[CapturedWindow]" = Queue(maxsize=5000)
        self.running = False
        self.windows_dropped_reported = 0
        self.frames_rejected_crc = 0

    def start(self):
        self.running = True
        self._thread = threading.Thread(target=self._listen_loop, daemon=True)
        self._thread.start()
        print(f"[*] Stream receiver live on {self.host}:{self.port}. Awaiting DAQ node connection...")

    def stop(self):
        self.running = False

    def _decode_window(self, header_fields: tuple, payload: bytes) -> CapturedWindow:
        (
            _magic, _version, _msg_type, frontend, trace_id, n_samples, pretrigger_samples,
            sample_bytes, channel_count, x_inc, srate, y_inc, y_or, dropped, assembly_ns,
            wall_clock_us, _payload_len, _crc,
        ) = header_fields

        dtype = np.uint8 if sample_bytes == 1 else np.dtype("<u2")
        codes = np.frombuffer(payload, dtype=dtype)
        if channel_count > 1:
            codes = codes.reshape(-1, channel_count)
        volts = codes.astype(np.float64) * y_inc + y_or

        plaintext = self.plaintext_source(trace_id) if self.plaintext_source else None
        return CapturedWindow(
            trace_id=trace_id, frontend=frontend, n_samples=n_samples,
            pretrigger_samples=pretrigger_samples, sample_bytes=sample_bytes,
            channel_count=channel_count, x_increment_s=x_inc, sample_rate_hz=srate,
            y_increment=y_inc, y_origin=y_or, windows_dropped=dropped, assembly_ns=assembly_ns,
            wall_clock_us=wall_clock_us, codes=codes, volts=volts, plaintext=plaintext,
        )

    def _listen_loop(self):
        server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server_socket.bind((self.host, self.port))
        server_socket.listen(1)
        server_socket.settimeout(1.0)

        while self.running:
            try:
                conn, addr = server_socket.accept()
            except socket.timeout:
                continue
            conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            print(f"[+] DAQ node connected from {addr}")
            buffer = bytearray()
            try:
                while self.running:
                    data = conn.recv(65536)
                    if not data:
                        break
                    buffer.extend(data)
                    self._drain_buffer(buffer)
            except Exception as e:
                print(f"[-] Connection dropped or error encountered: {e}")
            finally:
                conn.close()

    def _drain_buffer(self, buffer: bytearray):
        """Parse as many complete, CRC-clean packets out of buffer as are available,
        resyncing on the magic on any corruption rather than tearing down the link."""
        while True:
            sync = buffer.find(MAGIC)
            if sync == -1:
                if len(buffer) > 3:
                    del buffer[: len(buffer) - 3]  # keep a tail in case magic spans this chunk boundary
                return
            if sync > 0:
                del buffer[:sync]  # drop garbage before the sync word

            if len(buffer) < HEADER_SIZE:
                return
            header_bytes = bytes(buffer[:HEADER_SIZE])
            fields = struct.unpack(HEADER_FORMAT, header_bytes)
            magic, version, msg_type, _fe, _tid, _ns, _pre, _sb, _cc, _xi, _sr, _yi, _yo, _dr, _an, _wc, payload_len, crc = fields

            if magic != MAGIC or version != PROTO_VERSION or not _verify_header_crc(header_bytes, crc) \
                    or payload_len > self.max_payload:
                self.frames_rejected_crc += 1
                del buffer[:1]  # one byte at a time - a false-positive magic match elsewhere still resyncs
                continue

            total_len = HEADER_SIZE + payload_len + TRAILER_SIZE
            if len(buffer) < total_len:
                return  # wait for the rest of this packet

            payload = bytes(buffer[HEADER_SIZE:HEADER_SIZE + payload_len])
            trailer = bytes(buffer[HEADER_SIZE + payload_len:total_len])
            del buffer[:total_len]

            (payload_crc,) = struct.unpack(TRAILER_FORMAT, trailer)
            if crc16_ccitt_false(payload) != payload_crc:
                self.frames_rejected_crc += 1
                continue

            self.windows_dropped_reported = fields[13]  # windows_dropped
            if msg_type != MSG_WINDOW or payload_len == 0:
                continue  # heartbeat: telemetry only, no window

            window = self._decode_window(fields, payload)
            if not self.packet_queue.full():
                self.packet_queue.put(window)

    def gather_batch(self, batch_size: int, timeout: Optional[float] = None) -> List[CapturedWindow]:
        """Block until batch_size windows have accumulated (or timeout, if given - raises
        queue.Empty on expiry mid-batch)."""
        print(f"[*] Accumulating {batch_size} windows from queue...")
        windows = []
        while len(windows) < batch_size:
            windows.append(self.packet_queue.get(timeout=timeout))
        return windows


def windows_to_volts_matrix(windows: List[CapturedWindow]) -> np.ndarray:
    """Stack same-length single-channel windows into a (n_traces, n_samples) matrix - the
    shape dpa_cpa_engine.py's alignment / filtering / TVLA / CPA functions all expect."""
    lens = {w.n_samples for w in windows}
    if len(lens) != 1:
        raise ValueError(f"windows have mixed n_samples {sorted(lens)}; trim/pad before building a matrix")
    if windows[0].channel_count != 1:
        raise ValueError("windows_to_volts_matrix is single-channel; index .volts[..., ch] per window for multi-channel")
    return np.stack([w.volts for w in windows])


def windows_with_plaintext(windows: List[CapturedWindow]) -> Tuple[Optional[np.ndarray], Optional[np.ndarray]]:
    """(volts_matrix, plaintexts_matrix) built only from windows whose plaintext_source
    resolved a value - (None, None) if none did. Feeds directly into
    dpa_cpa_engine.recover_full_key() / generate_hamming_distance_hypotheses()."""
    have_pt = [w for w in windows if w.plaintext is not None]
    if not have_pt:
        return None, None
    volts = windows_to_volts_matrix(have_pt)
    plaintexts = np.array([np.frombuffer(w.plaintext, dtype=np.uint8) for w in have_pt])
    return volts, plaintexts


if __name__ == "__main__":
    receiver = SideChannelStreamReceiver(port=8080)
    receiver.start()
    try:
        windows = receiver.gather_batch(batch_size=200)
        volts = windows_to_volts_matrix(windows)
        print(f"[+] Matrix built: {volts.shape[0]} traces x {volts.shape[1]} samples")
        print(f"    frontend={windows[0].frontend} sample_rate_hz={windows[0].sample_rate_hz:.3g} "
              f"y_increment={windows[0].y_increment:.6g} frames_rejected_crc={receiver.frames_rejected_crc}")
        pt_volts, plaintexts = windows_with_plaintext(windows)
        if plaintexts is not None:
            print(f"[+] {plaintexts.shape[0]} windows carry a plaintext - ready for CPA key recovery")
        else:
            print("[*] No plaintext_source wired up - run TVLA/SNR (dpa_cpa_engine.py), not CPA, on this batch")
    except KeyboardInterrupt:
        pass
    finally:
        receiver.stop()
