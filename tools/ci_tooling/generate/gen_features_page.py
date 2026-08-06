#!/usr/bin/env python3
"""Generate docs/features.html - the browsable feature index for the docs site.

The README carries a summary and links here; this page is the whole set. It reads the same
docs/FEATURES.md and the same taxonomy the README tables are built from (tools/ci_tooling/lib/
feature_taxonomy.py), so the two cannot disagree about what exists or which layer it belongs to.

Why a page and not more markdown: 150+ features in a markdown table is a wall nobody reads. Here the
set is filterable, the rows are one line each until you want more, and the whole tree is bound in a
scroll box so the page itself stays a page.

    python -m tools.ci_tooling.generate.gen_features_page
"""

import html
import os


from tools.ci_tooling.lib import feature_taxonomy as tax
from tools.ci_tooling.lib import doc_region as dr

ROOT = dr.repo_root(__file__)
OUT = os.path.join(ROOT, "docs", "features.html")

# One accent per layer, walking cool -> warm as you climb the stack, so the eye can tell depth
# without reading. These are the same hues the request-lifecycle diagram uses for its layers.
GROUP_HUE = {
    "Foundation": "#8b93a7",
    "Physical & Data Link (L1-L2)": "#f97316",
    "Network (L3)": "#eab308",
    "Transport (L4)": "#f59e0b",
    "Session (L5)": "#10b981",
    "Presentation (L6)": "#3b82f6",
}
DEFAULT_HUE = "#6366f1"  # the application-layer categories

CSS = """
*, *::before, *::after { box-sizing: border-box; }

:root {
  --bg:        #0b0f14;
  --bg-raised: #121924;
  --bg-sunk:   #090d12;
  --rule:      #1e2836;
  --rule-hi:   #2b3846;
  --text:      #c2ced9;
  --bright:    #eaf1f8;
  --dim:       #7d8b9a;
  --accent:    #22d3ee;
  --radius:    8px;
  --sans: ui-sans-serif, system-ui, -apple-system, "Segoe UI", Inter, Roboto, sans-serif;
  --mono: "Fira Code", ui-monospace, SFMono-Regular, Menlo, Consolas, monospace;
}

html { scroll-behavior: smooth; }

body {
  margin: 0;
  background: var(--bg);
  color: var(--text);
  font: 15px/1.55 var(--sans);
  -webkit-font-smoothing: antialiased;
}

a { color: var(--accent); text-decoration: none; }
a:hover { text-decoration: underline; }

.wrap { max-width: 1180px; margin: 0 auto; padding: 0 20px 64px; }

/* ---------- masthead ---------- */
header.top {
  position: sticky; top: 0; z-index: 20;
  background: color-mix(in oklab, var(--bg) 86%, transparent);
  backdrop-filter: blur(12px);
  border-bottom: 1px solid var(--rule);
}
.top-in { max-width: 1180px; margin: 0 auto; padding: 14px 20px; display: flex; gap: 18px; align-items: center; flex-wrap: wrap; }
.brand { font-weight: 650; color: var(--bright); letter-spacing: -0.01em; margin-right: auto; }
.brand span { color: var(--dim); font-weight: 400; }

/* ---------- search ---------- */
.search { position: relative; flex: 1 1 260px; max-width: 380px; }
.search input {
  width: 100%; padding: 8px 12px 8px 34px;
  background: var(--bg-sunk); color: var(--bright);
  border: 1px solid var(--rule-hi); border-radius: var(--radius);
  font: 13px/1 var(--mono);
  transition: border-color .18s ease, box-shadow .18s ease;
}
.search input::placeholder { color: var(--dim); }
.search input:focus {
  outline: none; border-color: var(--accent);
  box-shadow: 0 0 0 3px color-mix(in oklab, var(--accent) 18%, transparent);
}
.search svg { position: absolute; left: 10px; top: 50%; transform: translateY(-50%); color: var(--dim); pointer-events: none; }

.count { font: 12px/1 var(--mono); color: var(--dim); white-space: nowrap; }
.count b { color: var(--bright); font-weight: 600; }

/* ---------- layer chips ---------- */
.chips { display: flex; gap: 6px; flex-wrap: wrap; padding: 12px 0 4px; }
.chip {
  --hue: var(--dim);
  display: inline-flex; align-items: center; gap: 7px;
  padding: 5px 11px; border-radius: 999px; cursor: pointer;
  background: transparent; border: 1px solid var(--rule-hi); color: var(--dim);
  font: 12px/1 var(--sans);
  transition: color .18s ease, border-color .18s ease, background .18s ease, transform .12s ease;
}
.chip::before { content: ""; width: 7px; height: 7px; border-radius: 50%; background: var(--hue); opacity: .85; }
.chip:hover { color: var(--bright); border-color: var(--hue); transform: translateY(-1px); }
.chip[aria-pressed="true"] {
  color: var(--bright);
  border-color: var(--hue);
  background: color-mix(in oklab, var(--hue) 14%, transparent);
}
.chip i { font-style: normal; color: var(--dim); font-family: var(--mono); font-size: 11px; }

/* ---------- the scroll box that binds the tree ---------- */
.tree {
  margin-top: 14px;
  border: 1px solid var(--rule); border-radius: var(--radius);
  background: linear-gradient(var(--bg-raised), var(--bg-raised)) padding-box;
  max-height: min(72vh, 900px);
  overflow-y: auto; overscroll-behavior: contain;
  scrollbar-width: thin; scrollbar-color: var(--rule-hi) transparent;
}
.tree::-webkit-scrollbar { width: 10px; }
.tree::-webkit-scrollbar-thumb { background: var(--rule-hi); border-radius: 999px; border: 3px solid var(--bg-raised); }
.tree::-webkit-scrollbar-thumb:hover { background: #3b4a5c; }

/* ---------- group ---------- */
.grp { border-bottom: 1px solid var(--rule); }
.grp:last-child { border-bottom: 0; }
.grp > h2 {
  --hue: var(--dim);
  position: sticky; top: 0; z-index: 5; margin: 0;
  display: flex; align-items: center; gap: 10px;
  padding: 9px 16px;
  background: color-mix(in oklab, var(--bg-raised) 92%, transparent);
  backdrop-filter: blur(8px);
  border-bottom: 1px solid var(--rule);
  font: 600 12px/1.2 var(--sans); letter-spacing: .06em; text-transform: uppercase;
  color: var(--bright);
}
.grp > h2::before { content: ""; width: 3px; height: 15px; border-radius: 2px; background: var(--hue); }
.grp > h2 em { margin-left: auto; font-style: normal; font: 11px/1 var(--mono); color: var(--dim); }

/* ---------- rows ---------- */
.row {
  display: grid; grid-template-columns: minmax(150px, 220px) 1fr;
  gap: 4px 18px; align-items: baseline;
  padding: 7px 16px;
  border-left: 2px solid transparent;
  transition: background .16s ease, border-color .16s ease;
}
.row + .row { border-top: 1px solid color-mix(in oklab, var(--rule) 55%, transparent); }
.row:hover { background: color-mix(in oklab, var(--accent) 5%, transparent); border-left-color: var(--hue, var(--accent)); }
.row:hover .name { color: var(--bright); }
.row:hover .more { opacity: 1; }

.name { color: var(--text); font-weight: 550; transition: color .16s ease; }
.flag { display: block; font: 11px/1.4 var(--mono); color: var(--dim); word-break: break-all; }
.flag.core { color: #3ecf7a; }
.desc {
  color: var(--dim); font-size: 13.5px;
  display: -webkit-box; -webkit-line-clamp: 2; -webkit-box-orient: vertical; overflow: hidden;
}
.row.open .desc { -webkit-line-clamp: unset; color: var(--text); }
.more {
  grid-column: 2; justify-self: start; margin-top: 2px;
  background: none; border: 0; padding: 0; cursor: pointer;
  color: var(--accent); font: 11px/1 var(--sans); opacity: 0;
  transition: opacity .16s ease;
}
.row.open .more { opacity: 1; }

.empty { padding: 28px 16px; color: var(--dim); text-align: center; }
.empty[hidden] { display: none; }

footer { padding-top: 22px; color: var(--dim); font-size: 12.5px; }

@media (max-width: 620px) {
  .row { grid-template-columns: 1fr; gap: 2px; }
  .more, .row.open .more { grid-column: 1; }
  .tree { max-height: none; }
}

@media (prefers-reduced-motion: reduce) {
  *, *::before, *::after { transition: none !important; animation: none !important; }
  html { scroll-behavior: auto; }
}
"""

JS = """
const q     = document.getElementById('q');
const rows  = [...document.querySelectorAll('.row')];
const grps  = [...document.querySelectorAll('.grp')];
const chips = [...document.querySelectorAll('.chip')];
const shown = document.getElementById('shown');
const empty = document.getElementById('empty');
let active = null;   // group filter, null = all

function apply() {
  const needle = q.value.trim().toLowerCase();
  let n = 0;
  for (const r of rows) {
    const okText  = !needle || r.dataset.hay.includes(needle);
    const okGroup = !active || r.dataset.grp === active;
    const ok = okText && okGroup;
    r.hidden = !ok;
    if (ok) n++;
  }
  // A group with nothing left in it should not leave its header floating.
  for (const g of grps) g.hidden = ![...g.querySelectorAll('.row')].some(r => !r.hidden);
  shown.textContent = n;
  empty.hidden = n !== 0;
}

q.addEventListener('input', apply);

for (const c of chips) {
  c.addEventListener('click', () => {
    active = (active === c.dataset.grp) ? null : c.dataset.grp;
    for (const o of chips) o.setAttribute('aria-pressed', String(o.dataset.grp === active));
    apply();
  });
}

// Expand one row's full description in place; the clamp is the default so the list stays scannable.
for (const r of rows) {
  const b = r.querySelector('.more');
  if (!b) continue;
  b.addEventListener('click', () => {
    r.classList.toggle('open');
    b.textContent = r.classList.contains('open') ? 'less' : 'more';
  });
}

// '/' focuses the filter, Escape clears it - the two keys anyone reaching for search already tries.
addEventListener('keydown', e => {
  if (e.key === '/' && document.activeElement !== q) { e.preventDefault(); q.focus(); }
  else if (e.key === 'Escape' && document.activeElement === q) { q.value = ''; apply(); q.blur(); }
});

apply();
"""


def clamp(text, limit=260):
    """FEATURES.md descriptions run to full paragraphs; the row shows a lead-in and the rest opens
    on demand. Cut on a sentence when one is near the limit, else on a word."""
    text = " ".join(text.split())
    if len(text) <= limit:
        return text, ""
    dot = text.rfind(". ", 0, limit + 60)
    cut = dot + 1 if dot > limit * 0.5 else text.rfind(" ", 0, limit)
    return text[:cut].strip(), text[cut:].strip()


def main():
    entries = tax.parse_features()

    groups = {}
    for e in entries:
        groups.setdefault(tax.group_of(e["name"]), []).append(e)

    order = [g for g in tax.group_order() if g in groups]

    chips, blocks = [], []
    for g in order:
        items = sorted(groups[g], key=lambda x: x["name"].lower())
        hue = GROUP_HUE.get(g, DEFAULT_HUE)
        gid = tax.github_anchor(g)

        chips.append(
            f'<button class="chip" style="--hue:{hue}" data-grp="{html.escape(gid)}" '
            f'aria-pressed="false">{html.escape(g)} <i>{len(items)}</i></button>'
        )

        rows = []
        for e in items:
            lead, rest = clamp(e["desc"])
            flag = e["flag"]
            flag_html = (
                f'<code class="flag">{html.escape(flag)}</code>' if flag else '<code class="flag core">always on</code>'
            )
            hay = html.escape(f"{e['name']} {flag or ''} {e['desc']}".lower(), quote=True)
            more = '<button class="more" type="button">more</button>' if rest else ""
            full = html.escape(lead + (" " + rest if rest else ""))
            rows.append(
                f'    <div class="row" style="--hue:{hue}" data-grp="{html.escape(gid)}" data-hay="{hay}">'
                f'<div><span class="name">{html.escape(e["name"])}</span>{flag_html}</div>'
                f'<div class="desc">{full}</div>{more}</div>'
            )

        blocks.append(
            f'  <section class="grp" id="{html.escape(gid)}" style="--hue:{hue}">\n'
            f'    <h2 style="--hue:{hue}">{html.escape(g)}<em>{len(items)}</em></h2>\n'
            + "\n".join(rows)
            + "\n  </section>"
        )

    doc = f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ProtoCore - Feature index</title>
<meta name="description" content="Every compile-time feature in ProtoCore, by OSI layer, with its PC_ENABLE_* flag.">
<style>{CSS}</style>
</head>
<body>

<header class="top">
  <div class="top-in">
    <div class="brand">ProtoCore <span>/ features</span></div>
    <label class="search">
      <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4"
           stroke-linecap="round"><circle cx="11" cy="11" r="7"/><path d="M20 20l-3.5-3.5"/></svg>
      <input id="q" type="search" placeholder="filter  ( / )" autocomplete="off" spellcheck="false"
             aria-label="Filter features">
    </label>
    <div class="count"><b id="shown">{len(entries)}</b> / {len(entries)}</div>
  </div>
</header>

<main class="wrap">
  <div class="chips">{"".join(chips)}</div>

  <div class="tree">
{chr(10).join(blocks)}
    <p class="empty" id="empty" hidden>Nothing matches that filter.</p>
  </div>

  <footer>
    Every feature is a compile-time <code>PC_ENABLE_*</code> flag, default off unless marked
    <span style="color:#3ecf7a">always on</span>. Prose comes from
    <a href="https://github.com/dstroy0/ProtoCore/blob/main/docs/FEATURES.md">docs/FEATURES.md</a>;
    this page and the README tables are generated from it, so they cannot disagree.
    &nbsp;·&nbsp; <a href="https://github.com/dstroy0/ProtoCore">repository</a>
    &nbsp;·&nbsp; <a href="configurator.html">build configurator</a>
  </footer>
</main>

<script>{JS}</script>
</body>
</html>
"""

    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(doc)

    print(f"wrote {os.path.relpath(OUT, ROOT)}: {len(entries)} features in {len(order)} groups")


if __name__ == "__main__":
    main()
