#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generate the core-API request-lifecycle flowchart from the source.

The variable parts are read straight from the code so the picture cannot drift:

  - the public `PC` API methods, from src/protocore.h
    (access-specifier aware), bucketed into Register / Configure / Run / Respond;
  - the built-in application protocols, from the session registry
    src/network_drivers/session/proto_builtins.cpp (each `register_if(PROTO_x, ...)`);
  - the Layer-6 modules present on disk, from src/network_drivers/presentation/.

Those are placed into the fixed OSI request-lifecycle skeleton (transport L4 -> session L5
dispatch seam -> presentation L6 -> application L7 route dispatch -> response back out the
transport), which is the library's architecture (docs/ARCHITECTURE.md). The diagram is injected
into the README "Overview" between generated markers.

Run from the repo root:
    python ci_tooling/generate/gen_api_flow.py            # rewrite the README block
    python ci_tooling/generate/gen_api_flow.py --check    # CI: exit 1 if stale
"""

import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
import doc_region as dr  # noqa: E402  (path set above)

ROOT = dr.repo_root(__file__)
API_H = os.path.join(ROOT, "src", "protocore.h")
PROTO_CPP = os.path.join(ROOT, "src", "network_drivers", "session", "proto_builtins.cpp")
PRESENTATION = os.path.join(ROOT, "src", "network_drivers", "presentation")
README = os.path.join(ROOT, "README.md")
ARCH = os.path.join(ROOT, "docs", "ARCHITECTURE.md")

TOOL = dr.tool_id(__file__)
REGION = dr.Region(README, "API FLOW", TOOL)
# The detailed variant lives in the architecture / API doc (every method, protocol, and L6 module).
REGION_DETAIL = dr.Region(ARCH, "API FLOW DETAIL", TOOL)
# The diagrams embed as pre-rendered files (not a live ```mermaid block) so they show in the GitHub
# web UI, the GitHub mobile app, AND Doxygen - none of which render a mermaid fence.
#
# They are SVG. The old note here said an SVG could not be used because GitHub strips the
# foreignObject multi-line labels need - true only while htmlLabels is on. With htmlLabels:false the
# labels are native <text>/<tspan>, which survives sanitizing, so the vector ships: selectable type,
# sharp at any zoom, and immune to the measure-in-one-font / draw-in-another clipping that truncated
# a node's label in the rasters. ci_tooling/assets/render_diagrams.sh emits light + dark of each
# (PNG as well, for anywhere a raster is still wanted); a <picture> serves the theme-matching one.
DIAGRAMS = os.path.join(ROOT, "docs", "diagrams")

# name -> API group. First matching rule wins; a startswith() prefix or an exact-name set.
GROUPS = ["Register", "Configure", "Run", "Respond"]
RULES = [
    ("Register", ("on", "serve_static", "dav", "use")),
    ("Configure", ("set_", "tls_", "enable_", "require_")),
    ("Run", ("begin", "handle", "service_once", "stop", "defer")),
    ("Respond", ("send", "redirect", "serve_file", "stats", "metrics", "sse_send", "diag")),
]
# A method declaration: `identifier( ...args... ) [const];` - args may span lines but hold no ;{}.
DECL = re.compile(r"\b([a-z]\w*)\s*\([^;{}]*\)\s*(?:const\s*)?;", re.S)
NOT_METHODS = {"if", "for", "while", "switch", "return", "sizeof", "operator"}
# Declared in the same header but not the user's API: the dispatch plumbing, the auth internals, and
# the *_internal / pump / trampoline seams the server calls on itself. They share prefixes with real
# entry points, so the bucket rules alone would pull them into the picture.
INTERNAL = {
    "serve_file_internal", "serve_static_request", "serve_dav_request", "try_serve_dav",
    "dispatch_matched_route", "match_and_execute", "route_admits", "fill_route_base",
    "path_matches", "regex_match", "run_middleware", "authorize_request",
    "send_unauth", "check_basic_auth", "check_digest_auth", "make_digest_nonce",
    "verify_digest_nonce", "regen_digest_secret", "rate_limit_check", "keepalive_eval",
    "chunk_send_pump", "file_send_pump", "http_poll_slot", "http_rfc1123", "status_text",
    "note_response", "append_resp_trailer", "req_is_head", "mime_type",
    "dav_send_status", "dav_stream_put_begin", "dav_stream_put_data", "dav_put_abort_tramp",
    "handle_ws_route", "ws_do_upgrade", "ws_dispatch_message", "ws_dispatch_close",
    "ws_send_version_required", "pc_sse_do_upgrade", "pc_csrf_gate",
}


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)  # block comments (incl. @code braces)
    return re.sub(r"//[^\n]*", "", text)  # line comments


def class_body(text, name):
    """The brace-matched body of `class <name> { ... }` (its definition, not a forward decl)."""
    m = re.search(r"\bclass\s+" + re.escape(name) + r"\b[^;{]*\{", text, re.S)
    if not m:
        raise SystemExit(f"class {name} definition not found")
    depth, start = 0, m.end() - 1  # points at the opening brace
    for idx in range(start, len(text)):
        depth += (text[idx] == "{") - (text[idx] == "}")
        if depth == 0:
            return text[start + 1 : idx]
    raise SystemExit(f"class {name} body not closed")


def public_methods():
    """Parse the library's entry points from protocore.h, in bucket order.

    There is no class to read the public section of any more - the API is free functions, so the
    whole header is the surface. `public:` used to be the filter that kept the diagram to what a
    user calls; with it gone, the filter is the bucket RULES below plus INTERNAL, because the header
    also declares plumbing that shares a prefix with the entry points (serve_file_internal shares
    "serve_", send_unauth shares "send"). A diagram of the public API must not teach a function
    nobody outside the library should call.
    """
    text = strip_comments(open(API_H, encoding="utf-8").read())
    names = []
    for mm in DECL.finditer(text):
        name = mm.group(1)
        if name in NOT_METHODS or name in INTERNAL or name in names:
            continue
        names.append(name)
    buckets = {g: [] for g in GROUPS}
    for n in names:
        for group, keys in RULES:
            if any(n == k or (k.endswith("_") and n.startswith(k)) or n.startswith(k) for k in keys):
                buckets[group].append(n)
                break
    return buckets


def protocols():
    """Parse the built-in protocols from the session registry (name, always_on)."""
    text = open(PROTO_CPP, encoding="utf-8").read()
    out = []
    for pm in re.finditer(r"register_if\(PROTO_(\w+),", text):
        name = pm.group(1)
        # "always present" ones carry that note on their register line; the rest sit behind a flag.
        line = text[text.rfind("\n", 0, pm.start()) + 1 : text.find("\n", pm.start())]
        out.append((name, "always present" in line))
    return out


def presentation_modules():
    """Layer-6 module directories that actually exist on disk."""
    if not os.path.isdir(PRESENTATION):
        return []
    return sorted(d for d in os.listdir(PRESENTATION) if os.path.isdir(os.path.join(PRESENTATION, d)))


def label(title, names, cap=2):
    """A compact multi-line node label: a title, up to `cap` method names, then a +N tail.

    Short labels are the single biggest lever against the wide, overlapping nodes long lists produce;
    <br/> wraps so a node grows in height (cheap) instead of width (expensive). ASCII separators only
    (a middle-dot can trip GitHub's Mermaid parser). `cap=None` lists every method (the detailed doc
    variant), wrapping three per line so the node stays a sane width.
    """
    methods = [f"{n}()" for n in names]
    if cap is None:
        rows = [" / ".join(methods[i : i + 3]) for i in range(0, len(methods), 3)]
        return title + "<br/>" + ("<br/>".join(rows) if rows else "-")
    body = " / ".join(methods[:cap]) if methods else "-"
    tail = f"<br/>+{len(names) - cap} more" if len(names) > cap else ""
    return f"{title}<br/>{body}{tail}"


def short(names, cap=3):
    """A ' / '-joined, capped list with a +N tail (ASCII only - a middle-dot trips GitHub's parser)."""
    body = " / ".join(names[:cap])
    return body + (f" / +{len(names) - cap}" if len(names) > cap else "")


def mermaid(detailed=False):
    """The request-lifecycle flowchart. `detailed=False` (README) caps method lists and hides the module
    inventory for a beginner-friendly picture; `detailed=True` (the API doc) lists every scraped method,
    every protocol, and every L6 module - the same clean waterfall, fully expanded."""
    api = public_methods()
    protos = protocols()
    pres = presentation_modules()
    cap = None if detailed else 2
    proto_short = short([p for p, _ in protos], cap=99 if detailed else 3)

    # Layout only (curved edges, roomy spacing). Deliberately NO 'theme' override: that lets GitHub swap
    # its light / dark Mermaid theme automatically, and the classDefs below use translucent fills with no
    # fixed text color so a node reads on either background (the page tints through the glassy fill).
    init = (
        # Root-level as well as under 'flowchart': the flowchart-scoped setting does not reach nodes
        # inside a subgraph, so the three SETUP boxes were still emitting foreignObject while every
        # other node had switched to <text>. Both, or GitHub renders that panel empty.
        "%%{init: {'htmlLabels':false,'themeVariables':{"
        # ONE font, and a generic one. A stack whose earlier entries the renderer does not have
        # (ui-sans-serif / system-ui / Segoe UI / Roboto are absent from a headless Chromium) makes
        # mermaid measure the label in the fallback and then draw it in whatever the rasterizer
        # picks - the box comes out narrower than its own text. All-caps labels overflow first,
        # which is how "YOUR HANDLER runs" rendered as "YOUR HANDLER ru". Measuring and drawing in
        # the same font is the whole fix, and it also makes the PNGs identical across the three
        # machines that can render them.
        "'fontFamily':'sans-serif','fontSize':'13px',"
        "'lineColor':'#94a3b8'},"  # a soft slate-gray edge color, gentle on both light and dark (not harsh black)
        # htmlLabels:false is what lets these ship as SVG. With it on, mermaid wraps every label in a
        # <foreignObject> holding real HTML - which GitHub's sanitizer strips, so the boxes arrive
        # empty. Off, a label is native SVG <text>/<tspan>, which survives sanitizing, stays
        # selectable, scales without rasterizing, and cannot be clipped by a font the renderer
        # measured with but did not draw with (the bug that ate "YOUR HANDLER runs").
        "'flowchart':{'curve':'basis','nodeSpacing':42,'rankSpacing':50,'padding':10,"
        "'useMaxWidth':true,'htmlLabels':false}}}%%"
    )
    out = [init, "flowchart TB"]
    out.append("  %% Auto-generated from the public API, proto_builtins.cpp, and presentation/ on disk.")
    # A single top-to-bottom spine: request flows down to your handler, response flows back to the client.
    # No per-layer boxes (they stretch to enclose both the incoming and outgoing node of a layer, which
    # is what made this sprawl) - the layer is shown by color instead, per the key above the diagram.
    # Setup runs once at boot; keep it as a small reference panel, wired in with one faint dashed edge.
    out.append('  subgraph SETUP["First, set up your server (once, at boot)"]')
    out.append("    direction LR")
    out.append(f'    reg["{label("1 Register routes", api["Register"], cap)}"]')
    out.append(f'    cfg["{label("2 Set options", api["Configure"], cap)}"]')
    out.append(f'    run["{label("3 Start it", api["Run"], cap)}"]')
    out.append("  end")
    out.append("")
    # One straight waterfall: a client sends at the top, the request flows down to your handler, the
    # response flows on down, and a client receives at the bottom - so there are no long back-edges. The
    # bidirectional UDP socket is drawn as its receive + send directions for the same reason. Node color
    # is the OSI layer (L4 amber, L5 green seams, L6 blue, L7 indigo), per the key above the diagram.
    out.append('  cin(["A client sends a request<br/>browser / app / curl"])')
    out.append('  listen["Accept a connection<br/>listener_accept_cb"]')
    out.append('  ring[("Hold the bytes<br/>conn_pool + rx ring")]')
    out.append('  udprx["Receive a datagram<br/>pc_udp"]')
    out.append(f'  seam{{{{"Which protocol?<br/>ProtoHandler seam<br/>{proto_short}"}}}}')
    out.append('  tls["Decrypt + choose version<br/>pc_tls + ALPN"]')
    out.append('  parser["Read HTTP/1.1<br/>http_parser"]')
    out.append('  h2["Decode HTTP/2<br/>h2_conn"]')
    out.append('  h3["Decode HTTP/3<br/>quic_conn + h3_conn"]')
    out.append('  mae{{"Find the matching route<br/>match_and_execute"}}')
    out.append('  mw["Run your middleware"]')
    out.append('  routes[("Route table")]')
    # A plain rect, not mermaid's asymmetric `>"..."]`: that shape's notch is cut out of the label's
    # own width, so the most important node in the picture rendered as "YOUR HANDLER ru". The amber
    # fill already says this is the part you write; it does not need a second signal that truncates.
    out.append('  handler["YOUR HANDLER runs"]')
    out.append(f'  resp["{label("Build the response", api["Respond"], cap)}"]')
    out.append('  sink{{"Frame the reply per protocol<br/>resp_sink seam<br/>HTTP/1.1 / h2 / h3"}}')
    out.append('  consend["Write bytes back<br/>pc_conn_send"]')
    out.append('  udptx["Send a datagram<br/>pc_udp"]')
    out.append('  cout(["The client gets the response"])')
    if detailed:
        # The full L6 module inventory on disk, six per line, hung off the presentation layer.
        rows = "<br/>".join(" / ".join(pres[i : i + 6]) for i in range(0, len(pres), 6))
        out.append(f'  mods["All L6 presentation modules ({len(pres)})<br/>{rows}"]')
    out.append("")

    # Edges. Track response-path edge indices so linkStyle can tint them a distinct color.
    edges = []
    res = []

    def edge(line, is_res=False):
        if is_res:
            res.append(len(edges))
        edges.append("  " + line)

    edge("run -.->|starts| listen")  # the one setup->flow link, faint + dashed
    # Request in: a client's bytes travel down to your handler.
    edge("cin ==>|TCP| listen")
    edge("listen --> ring")
    edge("ring --> seam")
    edge("cin ==>|UDP / QUIC| udprx")
    edge("udprx --> seam")
    edge("seam --> tls")
    edge("tls -->|HTTP/1.1| parser")
    edge("tls -->|ALPN h2| h2")
    edge("seam -->|HTTP/3| h3")
    edge("parser --> mae")
    edge("h2 --> mae")
    edge("h3 --> mae")
    edge("mae --> mw")
    edge("mw --> routes")
    edge("routes --> handler")
    edge("handler --> resp")
    # Response out (green): one seam frames the reply for whatever protocol, then it reaches the client.
    edge("resp --> sink", True)
    edge("sink -->|TCP: 1.1, h2| consend", True)
    edge("sink -->|QUIC: h3| udptx", True)
    edge("consend ==> cout", True)
    edge("udptx ==> cout", True)
    if detailed:
        edge("tls -.- mods")  # the module inventory hangs off the presentation layer (association, no arrow)
    out += edges
    out.append("")

    # Palette: one soft color per OSI layer, an accent for the two seams and the two you-touch parts.
    out.append("  class cin,cout ext;")  # class statement, not inline :::ext (GitHub rejects inline)
    out.append("  class reg,cfg,run,routes setup;")
    out.append("  class listen,ring,udprx,udptx,consend l4;")
    out.append("  class seam,sink seam;")
    out.append(f"  class tls,parser,h2,h3{',mods' if detailed else ''} l6;")
    out.append("  class mae,mw l7;")
    out.append("  class handler,resp you;")
    # Translucent fills (8-digit hex = ~15% alpha) + accent stroke + NO fixed text color, so the page
    # background (light or dark) shows through and the theme's own text color keeps every label readable.
    # The two seams (green) and the two parts you write (amber) stay solid so they still pop on both.
    out.append("  classDef ext fill:#64748b33,stroke:#475569,stroke-width:1.5px;")
    out.append("  classDef setup fill:#94a3b81f,stroke:#94a3b8;")
    out.append("  classDef l4 fill:#f9731626,stroke:#f97316,stroke-width:1.5px;")
    out.append("  classDef seam fill:#10b981,stroke:#047857,color:#ffffff;")
    out.append("  classDef l6 fill:#3b82f626,stroke:#3b82f6,stroke-width:1.5px;")
    out.append("  classDef l7 fill:#6366f126,stroke:#6366f1,stroke-width:1.5px;")
    out.append("  classDef you fill:#f59e0b,stroke:#b45309,color:#3b2508;")
    out.append("  style SETUP fill:#8888880f,stroke:#94a3b8,stroke-width:1px;")
    # Thicker links first (the default hairline is hard to follow), then tint the response path green so
    # "reply going out" is visually distinct from "request coming in".
    out.append("  linkStyle default stroke-width:2.5px;")
    out.append(f"  linkStyle {','.join(str(i) for i in res)} stroke:#10b981,stroke-width:3px;")
    return "\n".join(out)


def write_mmd(name, mmd):
    """Write the mermaid source to docs/diagrams/<name>.mmd (the render input + the editable source)."""
    os.makedirs(DIAGRAMS, exist_ok=True)
    with open(os.path.join(DIAGRAMS, name + ".mmd"), "w", encoding="utf-8", newline="\n") as f:
        f.write(mmd + "\n")


def picture(name, alt, rel, width=None, zoom=False):
    """A <picture> serving the dark SVG in dark mode and the light one otherwise (@ rel path prefix).

    SVG, not PNG. The labels are native <text> (htmlLabels:false in the source), so GitHub's
    sanitizer keeps them - the foreignObject problem that forced rasters here originally. Vector also
    means the type stays selectable and crisp at any zoom, and it cannot be clipped by the renderer
    measuring a label in one font and drawing it in another, which is what truncated "YOUR HANDLER
    runs" to "YOUR HANDLER ru" in the PNGs.

    This is a tall top-to-bottom waterfall, so at full column width it dominates the page. Pass
    `width` to cap the rendered size (GitHub honours `width` on <img>) and `zoom=True` to link to the
    standalone file, where it scales to the viewport instead of a fixed raster."""
    block = f'<img alt="{alt}" src="{rel}/{name}.svg"' + (f' width="{width}"' if width else "") + ">"
    if zoom:
        return f'<a href="{rel}/{name}.svg" title="Open the request-lifecycle diagram full size">\n  {block}\n</a>'
    return block


def build_block():
    write_mmd("api_flow", mermaid())
    return "\n".join(
        [
            "> Generated from the public API, `proto_builtins.cpp`, and `presentation/` by"
            " `ci_tooling/generate/gen_api_flow.py` - do not edit by hand. The picture is an SVG"
            " (native text labels, so it stays sharp at any zoom and the type is selectable); its mermaid source is"
            " [`docs/diagrams/api_flow.mmd`](docs/diagrams/api_flow.mmd).",
            "",
            "**How to read it:** follow the arrows. A **request comes in** at the top from a client, travels"
            " **down** through the four OSI layers - L4 wire bytes, L5 protocol pick, L6 decode into a request,"
            " L7 your routes - your handler runs, and the **response goes back out** along the **green** arrows."
            " Each box shows a plain-English step with the exact function underneath. You only write the two"
            " **amber** parts: register your routes (top) and your handler (middle).",
            "",
            "The one idea worth taking away: every HTTP version (1.1, 2, 3) is decoded into the *same* request"
            " and answered through *one* response seam, so your routes and handlers never care which protocol a"
            " client used.",
            "",
            "| Color | Layer |",
            "| --- | --- |",
            "| Amber outline | **L4 Transport** - raw bytes on/off the wire |",
            "| Green | **L5 Session** - the two seams that pick the protocol in and frame the reply out |",
            "| Blue | **L6 Presentation** - decrypt + turn bytes into a request |",
            "| Indigo | **L7 Application** - route matching + your handlers |",
            "| Solid amber fill | the parts **you** write |",
            "",
            picture(
                "api_flow",
                "Request lifecycle: a request travels down the OSI layers to your handler; the response returns",
                "docs/diagrams",
                width=460,
                zoom=True,
            ),
        ]
    )


def build_detail_block():
    write_mmd("api_flow_detail", mermaid(detailed=True))
    return "\n".join(
        [
            "> Generated from the public API, `proto_builtins.cpp`, and `presentation/` by"
            " `ci_tooling/generate/gen_api_flow.py` - do not edit by hand. This is the fully expanded twin of the"
            " simplified request-lifecycle chart in the [README](../README.md): the same top-to-bottom"
            " waterfall, but every public method, every registered protocol, and every Layer-6 module on disk"
            " is listed (nothing is capped). Color is the OSI layer; the green path is the response. Mermaid"
            " source: [`diagrams/api_flow_detail.mmd`](diagrams/api_flow_detail.mmd).",
            "",
            picture("api_flow_detail", "Full request lifecycle with every method, protocol, and module", "diagrams"),
        ]
    )


if __name__ == "__main__":
    chk = "--check" in sys.argv[1:]
    rc = dr.apply(README, {REGION: build_block()}, check=chk)
    rc |= dr.apply(ARCH, {REGION_DETAIL: build_detail_block()}, check=chk)
    raise SystemExit(rc)
