# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Modbus TCP interop with pymodbus (the de-facto reference Python stack).

Two roles:
  modbus-client : the *device is the slave*; this peer is a pymodbus master that
                  reads/writes coils and registers (FC 1/2/3/4/5/6/15/16).
  modbus-server : the *device is the master*; this peer is a pymodbus slave with
                  a known data model the device can read/write against.
"""

from __future__ import annotations

from ._common import Probe, require

# pymodbus moved the client/server classes around across 2.x/3.x; we import
# lazily inside run() via require() and probe for the available symbols.


# --------------------------------------------------------------------------- #
# modbus-client : probe the device acting as a Modbus slave
# --------------------------------------------------------------------------- #
class Client:
    NAME = "modbus-client"
    HELP = "read/write the device Modbus slave with a pymodbus master"

    @staticmethod
    def add_args(p) -> None:
        p.add_argument("--host", required=True, help="device IP / hostname")
        p.add_argument("--port", type=int, default=502, help="Modbus TCP port (default 502)")
        p.add_argument("--unit", type=int, default=1, help="unit/slave id (default 1)")
        p.add_argument("--timeout", type=float, default=3.0, help="response timeout seconds")

    @staticmethod
    def run(args) -> bool:
        require("pymodbus")
        from pymodbus.client import ModbusTcpClient  # pymodbus >= 3

        pr = Probe(f"modbus-client {args.host}:{args.port} unit={args.unit}")
        cli = ModbusTcpClient(args.host, port=args.port, timeout=args.timeout)
        # pymodbus renamed the slave/unit keyword repeatedly (unit -> slave -> device_id);
        # discover the name this version uses from the method signature so the peer works
        # across 3.x without pinning a version. count is keyword-only in 3.7+.
        unit_kw = _unit_kw(cli.read_holding_registers, args.unit)
        try:
            pr.check("TCP connect", cli.connect(), f"{args.host}:{args.port}")

            rr = cli.read_holding_registers(0, count=4, **unit_kw)
            pr.check("FC3 read holding registers", not rr.isError(), _vals(rr, "registers"))

            wr = cli.write_register(0, 0x1234, **unit_kw)
            pr.check("FC6 write single register", not wr.isError())
            rr = cli.read_holding_registers(0, count=1, **unit_kw)
            ok = not rr.isError() and getattr(rr, "registers", [None])[0] == 0x1234
            pr.check("read back written register", ok, _vals(rr, "registers"))

            wc = cli.write_coil(0, True, **unit_kw)
            pr.check("FC5 write single coil", not wc.isError())
            rc = cli.read_coils(0, count=1, **unit_kw)
            ok = not rc.isError() and bool(getattr(rc, "bits", [False])[0]) is True
            pr.check("read back written coil", ok, _vals(rc, "bits"))
        except Exception as exc:  # noqa: BLE001
            pr.check("session completed", False, str(exc))
        finally:
            cli.close()
        return pr.summary()


# --------------------------------------------------------------------------- #
# modbus-server : a reference slave the device (as master) can talk to
# --------------------------------------------------------------------------- #
class Server:
    NAME = "modbus-server"
    HELP = "run a pymodbus slave for the device Modbus master to read/write"

    @staticmethod
    def add_args(p) -> None:
        p.add_argument("--bind", default="0.0.0.0", help="listen address (default 0.0.0.0)")
        p.add_argument("--port", type=int, default=502, help="Modbus TCP port (default 502)")

    @staticmethod
    def run(args) -> bool:
        require("pymodbus")
        from pymodbus.datastore import (
            ModbusSequentialDataBlock,
            ModbusServerContext,
            ModbusSlaveContext,
        )
        from pymodbus.server import StartTcpServer

        # A small, predictable data model: 100 of each table, registers pre-seeded
        # 0,1,2,... so the device master can assert known values.
        block = lambda fill: ModbusSequentialDataBlock(0, fill)  # noqa: E731
        store = ModbusSlaveContext(
            di=block([i % 2 for i in range(100)]),
            co=block([0] * 100),
            hr=block(list(range(100))),
            ir=block([i * 2 for i in range(100)]),
        )
        context = ModbusServerContext(slaves=store, single=True)
        print(f"modbus-server: listening on {args.bind}:{args.port} (Ctrl-C to stop)")
        print("  data model: di=alternating, co=0, hr=0..99, ir=0,2,4,...")
        try:
            StartTcpServer(context=context, address=(args.bind, args.port))
        except KeyboardInterrupt:
            print("\nmodbus-server: stopped")
        return True


def _unit_kw(method, unit_id: int) -> dict:
    """Return {<unit-kwarg>: unit_id} using whichever name this pymodbus exposes.

    The slave-address keyword has been renamed across releases: unit= (<=3.6),
    slave= (3.7-3.8), device_id= (3.9+). Inspect the bound method's signature and
    pick the one it actually accepts so the peer is version-independent.
    """
    import inspect

    params = inspect.signature(method).parameters
    for name in ("device_id", "slave", "unit"):
        if name in params:
            return {name: unit_id}
    return {}  # very old pymodbus: unit defaulted positionally


def _vals(resp, attr: str) -> str:
    return str(getattr(resp, attr, resp))


# The dispatcher discovers multiple peers from one module via PEERS.
PEERS = [Client, Server]
