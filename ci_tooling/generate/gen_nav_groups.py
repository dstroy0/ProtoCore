#!/usr/bin/env python3
"""Group the sidebar into sections instead of 45 pages in encounter order.

Two problems in the tree as generated:

1. Seven roots were duplicates - docs/sphinx/{architecture,languages,osi-model,tcp-ip,
   src-law,symbols,index} are Sphinx sources for a DIFFERENT site, and Doxygen was reading
   them as pages. They shadow the real docs/ pages under lowercase names ("architecture",
   "symbols", a second "ProtoCore"). Excluded rather than grouped: they should never have
   been input.

2. The rest sat in whatever order Doxygen met them, so "Bug log" came third and "Examples"
   ninth. Now five hub pages claim them with @subpage, which is what gives the tree its
   sections and its order - @subpage order is the order they appear.

The grouping answers the question a reader actually arrives with: I am new / I need a fact /
I want to know how it works / is it correct and safe / where is this going.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
NAV = ROOT / "docs/nav"
NAV.mkdir(exist_ok=True)

GROUPS = [
    ("1-start", "Start here", "New to ProtoCore, or looking for something to run.", [
        ("md_docs_2EXAMPLES", "Examples"),
        ("md_docs_2HARDWARE__HOOKUP", "Hardware hookup and settings"),
        ("md_docs_2learn_2README", "Learn: how networking actually works"),
        ("md_docs_2learn_2osi-model", "The OSI model: the 7-layer map"),
        ("md_docs_2learn_2tcp-ip", "TCP/IP: the model the internet uses"),
        ("md_docs_2learn_2languages", "A primer on every language here"),
    ]),
    ("2-reference", "Reference", "Look up a flag, a number, a name, or a footprint.", [
        ("md_docs_2FEATURES", "Feature reference"),
        ("md_docs_2FEATURE__PERFORMANCE", "Feature performance"),
        ("md_docs_2FOOTPRINTS", "ESP32 build footprints"),
        ("md_docs_2TUNING", "Performance tuning"),
        ("md_docs_2SYMBOLS", "Symbols and naming"),
        ("md_docs_2reference_2README", "Espressif SoC datasheets"),
        ("md_docs_2THEMES", "Themes"),
        ("md_docs_2FAVICONS", "Favicons"),
    ]),
    ("3-how-it-works", "How it works", "The shape of the library and the rules its source obeys.", [
        ("md_docs_2ARCHITECTURE", "Architecture and internal data piping"),
        ("md_docs_2SRC__LAW", "The src/ law"),
        ("md_docs_2SRCBANNED", "Banned in src/, and what to use instead"),
        ("md_docs_2SSH", "SSH support"),
        ("md_docs_2DTLS", "DTLS 1.3 (RFC 9147)"),
    ]),
    ("4-correctness", "Correctness and security", "How the claims are checked, and by what.", [
        ("md_docs_2RFC", "RFC compliance"),
        ("md_docs_2AUDIT", "Standards conformance audit"),
        ("md_docs_2STANDARDS", "Standards and specifications"),
        ("md_docs_2INTEROP__MATRIX", "Interop harness matrix"),
        ("md_docs_2SECURITY", "Security documentation"),
        ("md_docs_2SECURE__BOOT", "Secure boot"),
        ("md_docs_2PENTEST", "Pentest"),
        ("md_docs_2CODEQL", "CodeQL static analysis"),
        ("md_docs_2SONARQUBE", "SonarQube / SonarCloud analysis"),
    ]),
    ("5-project", "Project", "Where it has been and where it is going.", [
        ("md_docs_2ROADMAP", "Roadmap"),
        ("md_docs_2TODO", "TODO / known fixes"),
        ("md_docs_2KNOWN__LIMITATIONS", "Known limitations"),
        ("md_docs_2BUGS", "Bug log"),
        ("md_docs_2DELIVERED", "Delivered"),
        ("md_docs_2CHANGELOG", "Changelog"),
    ]),
]

# Filenames carry a numeric prefix because Doxygen orders root pages by the order it
# reads them, which is filename order - without it "Start here" sorted fifth.
for slug, title, blurb, pages in GROUPS:
    body = [f"# {title}", "", blurb, ""]
    body += [f"- @subpage {pid} \"{label}\"" for pid, label in pages]
    body.append("")
    (NAV / f"{slug}.md").write_text("\n".join(body), encoding="utf-8")
print(f"docs/nav: {len(GROUPS)} hub pages claiming {sum(len(g[3]) for g in GROUPS)} pages")
