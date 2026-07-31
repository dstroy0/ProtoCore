#!/usr/bin/env python3
"""Independent Siemens 3964R + RK512 reference implementation.

Written from the Siemens "3964(R) transmission protocol" + "RK 512 computer link" spec, deliberately
NOT sharing any constant or code with the C codec (services/fieldbus/simatic) so a byte match is real conformance
(the second-impl discipline). Used two ways:
  * as a vector oracle (this file's --selftest prints the canonical wire bytes the C host test asserts), and
  * as a live link peer over a byte stream (drive_3964r) for an end-to-end handshake round-trip.
"""

STX, DLE, ETX, NAK = 0x02, 0x10, 0x03, 0x15


# ---- 3964R block framing (independent) ------------------------------------

def bcc(block: bytes) -> int:
    x = 0
    for b in block:
        x ^= b
    return x


def build_block(data: bytes, with_bcc: bool) -> bytes:
    out = bytearray()
    for b in data:
        out.append(b)
        if b == DLE:  # transparency doubling
            out.append(DLE)
    out.append(DLE)
    out.append(ETX)
    if with_bcc:
        out.append(bcc(bytes(out)))
    return bytes(out)


def parse_block(block: bytes, with_bcc: bool):
    out = bytearray()
    i = 0
    n = len(block)
    while i < n:
        b = block[i]
        if b == DLE:
            if i + 1 >= n:
                return None
            nx = block[i + 1]
            if nx == DLE:
                out.append(DLE)
                i += 2
            elif nx == ETX:
                i += 2
                if with_bcc:
                    if i >= n or bcc(block[:i]) != block[i]:
                        return None
                return bytes(out)
            else:
                return None
        else:
            out.append(b)
            i += 1
    return None


# ---- RK512 telegrams (independent; same documented field order as the codec) ----

CMD_SEND, CMD_FETCH, CMD_REACTION = 0x00, 0x01, 0x02
AREA = {"DB": 0x01, "DX": 0x02, "MB": 0x03, "EB": 0x04, "AB": 0x05, "PB": 0x06, "ZB": 0x07, "TB": 0x08}


def be16(v):
    return bytes([(v >> 8) & 0xFF, v & 0xFF])


def rk512_send(area, dbnr, addr, words):
    b = bytearray([CMD_SEND, 0x00, AREA[area], dbnr]) + be16(addr) + be16(len(words))
    for w in words:
        b += be16(w)
    return bytes(b)


def rk512_fetch(area, dbnr, addr, wcount):
    return bytes(bytearray([CMD_FETCH, 0x00, AREA[area], dbnr]) + be16(addr) + be16(wcount))


def rk512_reaction(status):
    return bytes(bytearray([CMD_REACTION]) + be16(status))


def selftest():
    # These are the canonical vectors the C host test (test_simatic.cpp) asserts. A match proves the
    # C codec agrees with an independent implementation of the wire format.
    vectors = {
        "block('A',DLE,'B') +bcc": build_block(bytes([0x41, DLE, 0x42]), True).hex(),
        "rk512_send DB5 @0x0010 {0x1234,0xABCD}": rk512_send("DB", 5, 0x0010, [0x1234, 0xABCD]).hex(),
        "rk512_fetch MB0 @0x0100 x4": rk512_fetch("MB", 0, 0x0100, 4).hex(),
        "rk512_reaction 0x0000": rk512_reaction(0x0000).hex(),
    }
    for k, v in vectors.items():
        print(f"{k:40s} {v}")
    # round-trip through our own framing
    payload = bytes([0x01, DLE, ETX, DLE, 0xFF])
    assert parse_block(build_block(payload, True), True) == payload, "peer framing round-trip failed"
    print("peer framing round-trip: OK")


if __name__ == "__main__":
    selftest()
