# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Shared feature taxonomy: the one curated grouping of features by OSI layer and
functional category, plus the FEATURES.md parser.

Both the README feature tables (ci_tooling/generate/gen_feature_tables.py) and the build
configurator (ci_tooling/generate/gen_configurator.py) group the same ~240 features, so the
grouping lives here once. A feature's home is decided by its FEATURES.md `## Heading`:
non-application headings are pinned to an OSI layer by LAYER_MEMBERS, application-layer
headings to a functional category by CATEGORY_MEMBERS. `group_order()` flattens that into
one ordered list of group labels (the six non-application layers, then the application
categories, then a trailing "Other" catch-all), which is the render order everywhere.

Editing one set here moves an entry in every generated artifact at once.
"""

import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FEATURES_MD = os.path.join(ROOT, "docs", "FEATURES.md")

# Render order of the OSI layers. Application (L7) is last and, being huge, is subdivided
# into the functional categories below rather than shown as one table/section.
LAYER_ORDER = [
    "Foundation",
    "Physical & Data Link (L1-L2)",
    "Network (L3)",
    "Transport (L4)",
    "Session (L5)",
    "Presentation (L6)",
    "Application (L7)",
]
APPLICATION_LAYER = "Application (L7)"

# FEATURES.md headings pinned to each non-application layer. Everything not listed here
# falls through to the application layer (most features are application-layer services).
LAYER_MEMBERS = {
    "Foundation": {
        "Config IO",
        "Config Store",
        "Device ID",
        "DMA Peripheral Ingest",
        "Exception Decoder",
        "Failsafe Watchdog",
        "GPIO Map",
        "Guardrails",
        "Hardware Health",
        "Preempting Work Queue",
        "PSRAM Pool",
        "RTC",
        "Sleep Scheduler",
        "Southbound",
        "Time Source",
        "VFS",
        "Wear Leveling",
    },
    "Physical & Data Link (L1-L2)": {
        "ADS1115",
        "BLE GATT",
        "Bus Capture",
        "CC1101",
        "DShot",
        "EnOcean",
        "ESP-NOW",
        "Ethernet",
        "FDC2214",
        "INA219",
        "Interface Forwarding",
        "LD2410",
        "LDC1614",
        "LoRa",
        "MPR121",
        "nRF24",
        "PCA9685",
        "PN532",
        "Radio Gateway",
        "Radio Power",
        "Radio Sniffer",
        "Raw L2",
        "SHT3x",
        "Sigfox",
        "Thread",
        "VL53L0X",
        "Wi-Fi Capture",
        "Wi-Fi Sniffer",
        "Wi-SUN",
        "Z-Wave",
        "Zigbee",
    },
    "Network (L3)": {
        "Dns Resolver",
        "Happy Eyeballs",
        "IPv6",
        "Link Manager",
        "Network Adaptation",
        "Proxy Protocol",
    },
    "Transport (L4)": {
        "Accept Throttle",
        "IP Allowlist",
        "Keep-Alive",
        "MTLS",
        "Per IP Throttle",
        "Socket Pool",
        "TLS",
        "TLS Policy",
        "TLS Resumption",
    },
    "Session (L5)": {
        "SSH",
        "SSH Compression",
        "SSH SFTP",
        "SSH SCP",
        "Telnet",
    },
    "Presentation (L6)": {
        "Auth",
        "Auth Lockout",
        "CBOR",
        "CloudEvents",
        "HTTP Delivery",
        "HTTP/1.1 Parser",
        "HTTP/2",
        "HTTP/3",
        "JSON",
        "JWT",
        "MessagePack",
        "Multipart",
        "Protobuf",
        "SenML",
        "SSE",
        "Web Terminal",
        "WebSocket",
        "WS Deflate",
    },
}

# The application layer holds ~125 features - too many for one group. Subdivide it into these
# functional categories (render order = CATEGORY_ORDER). Every application-layer heading should
# appear in exactly one set; one left out of all of them falls into a trailing "Other" group.
CATEGORY_ORDER = [
    "Web & HTTP",
    "Auth, Identity & Security",
    "IoT, Messaging & APIs",
    "Industrial & Fieldbus",
    "SCADA, Energy & Monitoring",
    "Machine Tools & OT",
    "Transportation & ITS",
    "Clients & Gateways",
    "Storage & Database",
    "Time & Discovery",
    "Observability & Telemetry",
    "Firmware & System",
]
CATEGORY_MEMBERS = {
    "Web & HTTP": {
        "Chunked Responses",
        "CORS",
        "Dashboard",
        "Edge Cache",
        "Edge Cache Mesh",
        "ETag",
        "File Serving",
        "HTTP Cache",
        "Middleware",
        "Range",
        "Routing",
        "SPA Router",
        "Templating",
        "Themes",
        "Upload",
        "WebDAV",
    },
    "Auth, Identity & Security": {
        "Audit Log",
        "CSRF",
        "OAuth2",
        "OIDC",
        "TOTP",
    },
    "IoT, Messaging & APIs": {
        "AMQP",
        "CoAP",
        "CoAP Block",
        "CoAP Observe",
        "DDS-RTPS",
        "GraphQL",
        "gRPC-Web",
        "LwM2M",
        "MQTT",
        "MQTT SN",
        "MQTT TLS",
        "NATS",
        "Sparkplug",
        "Stomp",
        "WAMP",
        "XMPP",
    },
    "Industrial & Fieldbus": {
        "ADS (Beckhoff)",
        "BACnet",
        "CANopen",
        "CiA 402",
        "Control",
        "CC-Link",
        "CIP",
        "COTP",
        "DeviceNet",
        "DF1",
        "DirectNET",
        "DMX512",
        "EtherNet/IP",
        "FINS",
        "HART",
        "Host Link",
        "INTERBUS",
        "IO-Link",
        "LonWorks",
        "MELSEC",
        "Modbus",
        "Modbus Master",
        "Modbus Plus",
        "Modbus RTU",
        "POWERLINK",
        "PROFIBUS",
        "PROFINET",
        "S7comm",
        "SDI-12",
        "SERCOS III",
        "SNP",
    },
    "SCADA, Energy & Monitoring": {
        "C37.118",
        "DNP3",
        "GOOSE",
        "ICCP",
        "IEC 60870",
        "M-Bus",
        "MMS",
        "OpenADR",
        "SEP2",
        "SunSpec",
    },
    "Machine Tools & OT": {
        "DNC (CNC drip-feed)",
        "MTConnect",
        "OPC-UA",
        "OPC-UA Client",
        "umati (OPC UA for Machine Tools)",
    },
    "Transportation & ITS": {
        "ATC",
        "J1939",
        "J2735",
        "NEMA TS2",
        "NMEA 0183",
        "NMEA 2000",
        "NTCIP",
        "OCIT",
        "UTMC",
        "WAVE",
    },
    "Clients & Gateways": {
        "FTP client",
        "HTTP Client",
        "HTTP Client TLS",
        "Relay (TCP forward / DNAT)",
        "SMB",
        "SMTP",
        "Webhook",
        "WS Client",
        "WS Client TLS",
    },
    "Storage & Database": {
        "DBM Key-Value Store",
        "Document Store",
        "Redis",
        "SQLite",
        "Write-Ahead Log",
    },
    "Time & Discovery": {
        "Adaptive mDNS",
        "DNS Server",
        "MDNS",
        "NTP",
        "NTP Server",
        "NTS",
        "PTP",
    },
    "Observability & Telemetry": {
        "Diag",
        "Flow Export",
        "Log-Buffer",
        "Metrics",
        "Observability",
        "Partition Monitor",
        "SNMP",
        "SNMP Trap",
        "SNMP V3",
        "Stats",
        "StatsD",
        "Syslog",
        "Telemetry",
        "UDP Telemetry",
    },
    "Firmware & System": {
        "OTA",
        "OTA Rollback",
        "Provisioning",
    },
}

# Label of the trailing catch-all for an application-layer heading in no category.
OTHER_GROUP = APPLICATION_LAYER + " - Other"


def github_anchor(heading):
    """Reproduce GitHub's heading-to-anchor rule (lowercase, drop punctuation
    other than spaces and hyphens, spaces to hyphens)."""
    a = heading.strip().lower()
    a = re.sub(r"[^a-z0-9 \-]", "", a)
    return a.replace(" ", "-")


def html_escape(text):
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")


def layer_of(name):
    for layer, members in LAYER_MEMBERS.items():
        if name in members:
            return layer
    return APPLICATION_LAYER


def category_of(name):
    for cat, members in CATEGORY_MEMBERS.items():
        if name in members:
            return cat
    return None


def group_of(name):
    """The single group label a feature heading belongs to: its OSI layer if
    non-application, else its functional category (or the Other catch-all)."""
    layer = layer_of(name)
    if layer != APPLICATION_LAYER:
        return layer
    return category_of(name) or OTHER_GROUP


def group_order():
    """Every group label in render order: the six non-application layers, then the
    application categories, then the Other catch-all."""
    return [ly for ly in LAYER_ORDER if ly != APPLICATION_LAYER] + list(CATEGORY_ORDER) + [OTHER_GROUP]


def parse_features(path=FEATURES_MD):
    """Return [{name, flag, desc, anchor}] in document order. `flag` is the optional
    `` `PC_ENABLE_*` `` line under the heading (or None); `desc` is the first prose
    paragraph (whitespace-collapsed)."""
    with open(path, "r", encoding="utf-8") as f:
        lines = f.read().splitlines()

    entries = []
    i = 0
    n = len(lines)
    while i < n:
        m = re.match(r"^## (.+?)\s*$", lines[i])
        if not m:
            i += 1
            continue
        name = m.group(1).strip()
        i += 1
        while i < n and not lines[i].strip():
            i += 1
        flag = None
        fm = re.match(r"^`(PC_[A-Z0-9_]+)`\s*$", lines[i].strip()) if i < n else None
        if fm:
            flag = fm.group(1)
            i += 1
            while i < n and not lines[i].strip():
                i += 1
        desc = []
        while i < n and lines[i].strip() and not lines[i].startswith("#"):
            desc.append(lines[i].strip())
            i += 1
        entries.append({"name": name, "flag": flag, "desc": " ".join(desc).strip(), "anchor": github_anchor(name)})
    return entries
