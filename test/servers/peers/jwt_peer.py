# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""JWT (RFC 7519, HS256) interop: validate the device's bearer-auth verifier with a real PyJWT client.

Role: the *device is the server*. The device verifies incoming HS256 bearer tokens for auth
(jwt_verify_hs256 / _at). This peer uses the third-party `PyJWT` library to mint tokens signed with the
shared secret and asserts the verifier's semantics via the rig's GET /jwt/verify?token= route:

  1. a valid HS256 token (correct secret, far-future exp) -> the device accepts it (sig=1, valid=1)
  2. a token signed with the WRONG secret -> the device rejects the signature (sig=0) - it really checks
  3. a correctly-signed but EXPIRED token -> signature ok but time-invalid (sig=1, valid=0)

That closes the loop: the device's HMAC-SHA256 verify + exp/nbf enforcement interoperate with a genuine JWT
library, and it fails closed on a bad signature or an expired token.
"""

from __future__ import annotations

import http.client
import json

from ._common import Probe, require

NAME = "jwt"
HELP = "validate the device's JWT HS256 bearer-auth verifier with a real PyJWT client (device-as-server)"

# Must match the rig's JWT_RIG_SECRET (penetration_testing/rig_firmware/src/main.cpp).
_SECRET = "pc-rig-jwt-secret-2026"


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP / hostname (the JWT verifier)")
    p.add_argument("--port", type=int, default=80, help="device HTTP port (default 80)")
    p.add_argument("--timeout", type=float, default=8.0, help="timeout seconds")


def run(args) -> bool:
    pr = Probe(f"jwt device={args.host}:{args.port}")
    pyjwt = require("jwt")  # PyJWT

    def verify(token: str):
        c = http.client.HTTPConnection(args.host, args.port, timeout=args.timeout)
        c.request("GET", "/jwt/verify", headers={"Authorization": f"Bearer {token}"})
        rep = json.loads(c.getresponse().read())
        c.close()
        return rep

    # 1. a valid HS256 token (correct secret, far-future exp, past nbf) is accepted.
    good = pyjwt.encode(
        {"sub": "rig", "role": "admin", "iat": 1600000000, "nbf": 1600000000, "exp": 2000000000},
        _SECRET,
        algorithm="HS256",
    )
    try:
        rep = verify(good)
    except Exception as exc:  # noqa: BLE001
        pr.check("device /jwt/verify completed", False, str(exc))
        return pr.summary()
    pr.check("valid HS256 token: signature ok", rep.get("sig") == 1, json.dumps(rep))
    pr.check("valid HS256 token: accepted (sig+time)", rep.get("valid") == 1, f"valid={rep.get('valid')}")

    # 2. a token signed with the WRONG secret must be rejected (the device really verifies the HMAC).
    forged = pyjwt.encode(
        {"sub": "attacker", "role": "admin", "exp": 2000000000}, "the-wrong-secret", algorithm="HS256"
    )
    rep = verify(forged)
    pr.check("wrong-secret token: signature rejected", rep.get("sig") == 0, f"sig={rep.get('sig')}")
    pr.check("wrong-secret token: not accepted", rep.get("valid") == 0, f"valid={rep.get('valid')}")

    # 3. a correctly-signed but EXPIRED token: signature ok, but time-invalid (exp in the past).
    expired = pyjwt.encode({"sub": "rig", "exp": 1000000000}, _SECRET, algorithm="HS256")  # year 2001
    rep = verify(expired)
    pr.check("expired token: signature ok", rep.get("sig") == 1, f"sig={rep.get('sig')}")
    pr.check(
        "expired token: rejected by exp check", rep.get("valid") == 0, f"valid={rep.get('valid')} now={rep.get('now')}"
    )

    return pr.summary()
