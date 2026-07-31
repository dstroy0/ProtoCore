#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generate the theme library: one CSS per palette in web_assets/themes/generated/.

Hand-writing a couple hundred theme stylesheets is not maintainable, so a theme is a **palette**
(a handful of colors + a light/dark mode) rendered through one shared **template** of CSS rules that
style the common page elements (body, headings, links, cards, buttons, inputs, tables, code). Every
theme therefore has the same structure and only its colors differ, which keeps them consistent and
lets the set grow to hundreds. The seven original hand-authored themes in web_assets/themes/*.css are
left untouched (they carry bespoke personality - CRT scanlines, nyan trails); this only fills
web_assets/themes/generated/.

Categories: `enterprise` (calm, professional), `editor` (recognizable code-editor schemes),
`retro` (terminals / 8-bit), `meme` (loud + fun), `pastel`, `nature`. Give the kids options and the
professionals a boardroom-safe default.

    python web_assets/wizard/gen_themes.py           # (re)generate web_assets/themes/generated/*.css
    python web_assets/wizard/gen_themes.py --check    # CI: fail if any generated theme is stale
    python web_assets/wizard/gen_themes.py --list      # print "name category mode" for every theme
"""

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

# Repo root, located by marker rather than by counting `..` levels: these scripts
# have moved once already (src/web/wizard -> web_assets/wizard) and level-counting
# silently retargeted them outside the tree.
def _repo_root(start):
    d = os.path.abspath(start)
    while d != os.path.dirname(d):
        if os.path.exists(os.path.join(d, "library.json")):
            return d
        d = os.path.dirname(d)
    raise RuntimeError("repo root not found (no library.json above %s)" % start)


REPO_ROOT = _repo_root(SCRIPT_DIR)

OUT_DIR = os.path.normpath(os.path.join(SCRIPT_DIR, "..", "themes", "generated"))

# A palette: name -> (category, mode, colors). colors = (bg, bg2, card, ink, muted, accent, accent2).
#   bg/bg2 = page background gradient stops   card = panel surface   ink = body text
#   muted  = secondary text / borders          accent = primary      accent2 = secondary / links-hover
# mode "dark" flips a couple of derived tones (input surface, code block). Hex only, 6-digit.
P = {}


def add(name, category, mode, bg, bg2, card, ink, muted, accent, accent2):
    P[name] = (category, mode, bg, bg2, card, ink, muted, accent, accent2)


# Themes named after a company / product trademark: kept in the OPEN-SOURCE library (a color palette is
# not the trademark), but a commercial build should not ship the branded name/association, so the theme
# blobs (gen_theme_blobs.py) gate these behind PC_THEMES_INCLUDE_TRADEMARKED - default 1 (open source),
# set 0 in a commercial build to drop them. Edit this set freely; it is the single source the packager
# and the gallery both read. Includes the seven bespoke themes by name where relevant.
RESTRICTED = frozenset(
    {
        "darcula",  # JetBrains
        "windows-xp",  # Microsoft
        "discord",  # Discord Inc.
        "slack-aubergine",  # Slack / Salesforce
        "spotify",  # Spotify
        "doom",  # id Software / Bethesda
        "minecraft",  # Mojang / Microsoft
        "barbie",  # Mattel
        "among-us",  # InnerSloth
        "pip-boy",  # Bethesda (Fallout)
        "gameboy",  # Nintendo
        "keroppi",  # Sanrio (bespoke)
        "nyancat",  # the Nyan Cat character (bespoke)
    }
)


# --- enterprise: calm, professional, boardroom-safe -------------------------------------------------
add(
    "corporate-slate",
    "enterprise",
    "light",
    "#f8fafc",
    "#eef2f7",
    "#ffffff",
    "#1e293b",
    "#64748b",
    "#2563eb",
    "#0ea5e9",
)
add("graphite", "enterprise", "light", "#f5f5f4", "#e7e5e4", "#ffffff", "#292524", "#78716c", "#334155", "#0891b2")
add("azure-pro", "enterprise", "light", "#f0f6ff", "#e2edfb", "#ffffff", "#0f2540", "#5b7699", "#0067c5", "#00a3e0")
add("ibm-carbon", "enterprise", "dark", "#161616", "#262626", "#262626", "#f4f4f4", "#8d8d8d", "#0f62fe", "#33b1ff")
add("mono-minimal", "enterprise", "light", "#ffffff", "#fafafa", "#ffffff", "#111111", "#8a8a8a", "#111111", "#555555")
add("forest-exec", "enterprise", "light", "#f4f7f4", "#e6efe6", "#ffffff", "#1b3a2b", "#5f7d6c", "#1a7a4c", "#0f766e")
add("navy-oxford", "enterprise", "dark", "#0b1f3a", "#0f2a4d", "#132f56", "#e8eef7", "#8ba3c1", "#3b82f6", "#22d3ee")
add("bordeaux", "enterprise", "light", "#faf6f6", "#f0e6e6", "#ffffff", "#3a1420", "#8a6a72", "#7a1f3d", "#a8324f")
add("sandstone", "enterprise", "light", "#faf7f0", "#f0e9da", "#ffffff", "#3a3226", "#8a7a5f", "#b45309", "#0e7490")
add("steel", "enterprise", "dark", "#1c2530", "#232f3c", "#2a3745", "#e6edf3", "#93a4b5", "#4f8cc9", "#5eb0d8")

# --- editor: recognizable code-editor color schemes -------------------------------------------------
add("dracula", "editor", "dark", "#282a36", "#21222c", "#343746", "#f8f8f2", "#6272a4", "#bd93f9", "#ff79c6")
add("nord", "editor", "dark", "#2e3440", "#3b4252", "#3b4252", "#eceff4", "#81a1c1", "#88c0d0", "#a3be8c")
add("solarized-dark", "editor", "dark", "#002b36", "#073642", "#073642", "#eee8d5", "#93a1a1", "#268bd2", "#2aa198")
add("solarized-light", "editor", "light", "#fdf6e3", "#eee8d5", "#ffffff", "#586e75", "#93a1a1", "#268bd2", "#cb4b16")
add("gruvbox-dark", "editor", "dark", "#282828", "#32302f", "#3c3836", "#ebdbb2", "#a89984", "#fabd2f", "#fe8019")
add("gruvbox-light", "editor", "light", "#fbf1c7", "#f2e5bc", "#ffffff", "#3c3836", "#7c6f64", "#b57614", "#af3a03")
add("monokai", "editor", "dark", "#272822", "#2d2e27", "#3e3d32", "#f8f8f2", "#75715e", "#a6e22e", "#f92672")
add("one-dark", "editor", "dark", "#282c34", "#21252b", "#2c313a", "#abb2bf", "#5c6370", "#61afef", "#c678dd")
add("one-light", "editor", "light", "#fafafa", "#f0f0f0", "#ffffff", "#383a42", "#a0a1a7", "#4078f2", "#a626a4")
add("tokyo-night", "editor", "dark", "#1a1b26", "#16161e", "#24283b", "#c0caf5", "#565f89", "#7aa2f7", "#bb9af7")
add("catppuccin-mocha", "editor", "dark", "#1e1e2e", "#181825", "#313244", "#cdd6f4", "#6c7086", "#89b4fa", "#f5c2e7")
add("catppuccin-latte", "editor", "light", "#eff1f5", "#e6e9ef", "#ffffff", "#4c4f69", "#8c8fa1", "#1e66f5", "#ea76cb")
add("catppuccin-frappe", "editor", "dark", "#303446", "#292c3c", "#414559", "#c6d0f5", "#737994", "#8caaee", "#f4b8e4")
add(
    "catppuccin-macchiato",
    "editor",
    "dark",
    "#24273a",
    "#1e2030",
    "#363a4f",
    "#cad3f5",
    "#6e738d",
    "#8aadf4",
    "#f5bde6",
)
add("github-dark", "editor", "dark", "#0d1117", "#161b22", "#161b22", "#c9d1d9", "#8b949e", "#58a6ff", "#a371f7")
add("github-light", "editor", "light", "#ffffff", "#f6f8fa", "#ffffff", "#24292f", "#6e7781", "#0969da", "#8250df")
add("ayu-dark", "editor", "dark", "#0a0e14", "#0d1017", "#141a22", "#b3b1ad", "#565b66", "#ffb454", "#59c2ff")
add("ayu-mirage", "editor", "dark", "#1f2430", "#232834", "#2a303c", "#cbccc6", "#707a8c", "#ffcc66", "#5ccfe6")
add("ayu-light", "editor", "light", "#fafafa", "#f3f4f5", "#ffffff", "#5c6773", "#abb0b6", "#ff9940", "#399ee6")
add("rose-pine", "editor", "dark", "#191724", "#1f1d2e", "#26233a", "#e0def4", "#6e6a86", "#c4a7e7", "#ebbcba")
add("rose-pine-dawn", "editor", "light", "#faf4ed", "#fffaf3", "#ffffff", "#575279", "#9893a5", "#907aa9", "#d7827e")
add("everforest", "editor", "dark", "#2b3339", "#323c41", "#3a464c", "#d3c6aa", "#859289", "#a7c080", "#e69875")
add("kanagawa", "editor", "dark", "#1f1f28", "#16161d", "#2a2a37", "#dcd7ba", "#727169", "#7e9cd8", "#e6c384")
add("night-owl", "editor", "dark", "#011627", "#0b2942", "#0e293f", "#d6deeb", "#637777", "#82aaff", "#c792ea")
add("palenight", "editor", "dark", "#292d3e", "#242837", "#323649", "#a6accd", "#676e95", "#c792ea", "#89ddff")
add("cobalt2", "editor", "dark", "#193549", "#1f4662", "#15232d", "#ffffff", "#8aa6c1", "#ffc600", "#ff9d00")
add("synthwave", "editor", "dark", "#262335", "#241b2f", "#34294f", "#ffffff", "#848bbd", "#ff7edb", "#36f9f6")
add("oceanic-next", "editor", "dark", "#1b2b34", "#22333b", "#2b3a42", "#d8dee9", "#65737e", "#6699cc", "#5fb3b3")
add("shades-of-purple", "editor", "dark", "#2d2b55", "#1e1e3f", "#38356e", "#ffffff", "#a599e9", "#fad000", "#ff628c")

# --- retro / terminal -------------------------------------------------------------------------------
add("amber-crt", "retro", "dark", "#0a0600", "#140d00", "#140d00", "#ffb000", "#8a6000", "#ffcc33", "#ff8800")
add("green-phosphor", "retro", "dark", "#001100", "#002200", "#002200", "#33ff66", "#1a8a33", "#66ff99", "#00cc44")
add("ibm-3270", "retro", "dark", "#0d0208", "#1a0410", "#1a0410", "#33ff33", "#1a8a1a", "#33ff33", "#00cc00")
add("c64", "retro", "dark", "#40318d", "#4a3a9e", "#4a3a9e", "#a5a0e0", "#7a72c0", "#a5a0e0", "#ffffff")
add("gameboy", "retro", "light", "#9bbc0f", "#8bac0f", "#9bbc0f", "#0f380f", "#306230", "#0f380f", "#306230")
add("hotdog-stand", "meme", "light", "#ff0000", "#ffff00", "#ffff00", "#000000", "#aa0000", "#000000", "#ff0000")

# --- meme / fun -------------------------------------------------------------------------------------
add("vaporwave", "meme", "dark", "#2d1b4e", "#3d2063", "#ff71ce", "#ffffff", "#b967ff", "#01cdfe", "#05ffa1")
add("matrix", "meme", "dark", "#000000", "#001100", "#001a00", "#00ff41", "#008f11", "#00ff41", "#22ff66")
add("barbie", "meme", "light", "#ffe0f0", "#ffd0ec", "#ffffff", "#a10c58", "#d16ba5", "#e91e8c", "#ff4fa3")
add("minecraft", "meme", "light", "#7fb238", "#8fc44a", "#8b6f47", "#3a2a16", "#6b5333", "#5a9e2f", "#8b6f47")
add("among-us", "meme", "dark", "#132a3e", "#1a3a52", "#c51111", "#ffffff", "#7a8b99", "#c51111", "#ef7d0d")
add("doge", "meme", "light", "#f5e6c8", "#f0dcae", "#ffffff", "#5a4a2a", "#a08a5a", "#f2a900", "#3fa7d6")
add("cyberpunk", "meme", "dark", "#0d0221", "#190b33", "#241540", "#f0f0ff", "#7b6ba8", "#fcee0a", "#ff003c")
add("bubblegum", "meme", "light", "#fff0f6", "#ffe0ee", "#ffffff", "#7a2a55", "#c86a95", "#ff5fa2", "#7ac7ff")

# --- pastel / nature --------------------------------------------------------------------------------
add("mint", "pastel", "light", "#eafaf1", "#d6f5e3", "#ffffff", "#1e4635", "#5f8a74", "#2ecc71", "#16a085")
add("lavender", "pastel", "light", "#f3f0fb", "#e8e2f7", "#ffffff", "#3a2f5a", "#8579a5", "#8b5cf6", "#ec4899")
add("peach", "pastel", "light", "#fff3ec", "#ffe6d6", "#ffffff", "#5a3a2a", "#a5836a", "#ff8a5c", "#ffb703")
add("sky", "pastel", "light", "#eef7ff", "#d9edff", "#ffffff", "#123a5a", "#5f88a5", "#3b9dff", "#00b4d8")
add("sakura", "nature", "light", "#fff5f7", "#ffe4ea", "#ffffff", "#6a2a3a", "#b57a88", "#ff6f91", "#845ec2")
add("autumn", "nature", "light", "#fbf3e8", "#f3e2cc", "#ffffff", "#4a2f1a", "#8a6a4a", "#c05621", "#8a6d3b")
add("deep-sea", "nature", "dark", "#031b2e", "#052a45", "#073a5c", "#cfe8f7", "#5a8aa8", "#00b4d8", "#48cae4")
add("volcano", "nature", "dark", "#1a0d0a", "#2a1410", "#3a1c16", "#ffddcc", "#a56a5a", "#ff5722", "#ffab00")

# --- more editor schemes (canonical palettes) -------------------------------------------------------
add("material-ocean", "editor", "dark", "#0f111a", "#0b0d14", "#1a1c25", "#a6accd", "#4b526d", "#82aaff", "#c792ea")
add("material-darker", "editor", "dark", "#212121", "#1a1a1a", "#2a2a2a", "#eeffff", "#616161", "#82aaff", "#c792ea")
add("tomorrow-night", "editor", "dark", "#1d1f21", "#282a2e", "#282a2e", "#c5c8c6", "#969896", "#81a2be", "#b294bb")
add("zenburn", "editor", "dark", "#3f3f3f", "#383838", "#4a4a4a", "#dcdccc", "#7f9f7f", "#f0dfaf", "#dca3a3")
add("molokai", "editor", "dark", "#1b1d1e", "#232526", "#293739", "#f8f8f2", "#465457", "#a6e22e", "#f92672")
add("darcula", "editor", "dark", "#2b2b2b", "#313335", "#3c3f41", "#a9b7c6", "#808080", "#cc7832", "#6897bb")
add("gruvbox-material", "editor", "dark", "#282828", "#32302f", "#3c3836", "#d4be98", "#928374", "#a9b665", "#d8a657")
add("panda", "editor", "dark", "#292a2b", "#242526", "#31353a", "#e6e6e6", "#676b79", "#ff9ac1", "#45a9f9")
add("horizon", "editor", "dark", "#1c1e26", "#16171e", "#232530", "#d5d8da", "#6c6f93", "#e95678", "#fab795")
add("andromeda", "editor", "dark", "#23262e", "#1e2025", "#2b2f38", "#d5ced9", "#746f77", "#00e8c6", "#ff5370")
add("bluloco-dark", "editor", "dark", "#282c34", "#21252b", "#2c313a", "#abb2bf", "#636d83", "#3691ff", "#ff6480")
add("city-lights", "editor", "dark", "#1d252c", "#171d23", "#28323a", "#b7c5d3", "#41505e", "#5ec4ff", "#e27e8d")
add("nightfly", "editor", "dark", "#011627", "#0e293f", "#0e293f", "#c3ccdc", "#7c8f8f", "#82aaff", "#c792ea")
add("moonlight", "editor", "dark", "#212337", "#1e2030", "#2f334d", "#c8d3f5", "#7a88cf", "#82aaff", "#c099ff")
add("poimandres", "editor", "dark", "#1b1e28", "#171922", "#252b37", "#e4f0fb", "#767c9d", "#5de4c7", "#add7ff")
add("aura", "editor", "dark", "#15141b", "#110f18", "#1c1b22", "#edecee", "#6d6d6d", "#a277ff", "#61ffca")
add("firewatch", "editor", "dark", "#1e2027", "#25282f", "#2c2f38", "#d5d8da", "#545864", "#dd6862", "#6cbcd8")
add("vitesse-dark", "editor", "dark", "#121212", "#181818", "#222222", "#dbd7ca", "#758575", "#4d9375", "#cb7676")
add("vitesse-light", "editor", "light", "#ffffff", "#f7f7f7", "#ffffff", "#393a34", "#a0ada0", "#1e754f", "#ab5959")

# --- more enterprise (design-system neutrals) -------------------------------------------------------
add("tailwind-zinc", "enterprise", "light", "#fafafa", "#f4f4f5", "#ffffff", "#18181b", "#71717a", "#6366f1", "#0ea5e9")
add(
    "tailwind-stone", "enterprise", "light", "#fafaf9", "#f5f5f4", "#ffffff", "#1c1917", "#78716c", "#0ea5e9", "#f59e0b"
)
add("neutral-dark", "enterprise", "dark", "#171717", "#0a0a0a", "#262626", "#fafafa", "#737373", "#3b82f6", "#22d3ee")
add("ibm-g10", "enterprise", "light", "#f4f4f4", "#e0e0e0", "#ffffff", "#161616", "#8d8d8d", "#0f62fe", "#0072c3")
add("ibm-g90", "enterprise", "dark", "#262626", "#333333", "#393939", "#f4f4f4", "#8d8d8d", "#4589ff", "#33b1ff")
add("vercel", "enterprise", "dark", "#000000", "#0a0a0a", "#111111", "#ededed", "#888888", "#0070f3", "#7928ca")
add("linear", "enterprise", "dark", "#0b0b0e", "#101014", "#18181c", "#e6e6e6", "#8a8f98", "#5e6ad2", "#26d0b0")
add("stripe", "enterprise", "light", "#f6f9fc", "#eef3f8", "#ffffff", "#32325d", "#6b7c93", "#635bff", "#00d4ff")
add("notion-dark", "enterprise", "dark", "#191919", "#202020", "#2f2f2f", "#d4d4d4", "#9b9b9b", "#529cca", "#e9a23b")
add("fluent-dark", "enterprise", "dark", "#201f1e", "#292827", "#323130", "#ffffff", "#a19f9d", "#2899f5", "#8a8886")

# --- more retro -------------------------------------------------------------------------------------
add("borland", "retro", "dark", "#0000aa", "#0000cc", "#0000aa", "#ffff55", "#5555ff", "#ffff55", "#55ffff")
add("pip-boy", "retro", "dark", "#0a1a0a", "#0f240f", "#0f240f", "#41ff00", "#1a7a1a", "#41ff00", "#88ff44")
add("zx-spectrum", "retro", "dark", "#000000", "#0000d7", "#0000d7", "#ffffff", "#00d7d7", "#ffd700", "#00d700")
add("commodore-pet", "retro", "dark", "#000000", "#0a1a0a", "#0a1a0a", "#4aff4a", "#2a8a2a", "#4aff4a", "#88ff88")

# --- more meme / brand-nostalgia --------------------------------------------------------------------
add("windows-xp", "meme", "light", "#3a6ea5", "#245edb", "#ece9d8", "#0a246a", "#7a96c8", "#316ac5", "#f5a623")
add("discord", "meme", "dark", "#313338", "#2b2d31", "#383a40", "#dbdee1", "#949ba4", "#5865f2", "#00a8fc")
add("slack-aubergine", "meme", "dark", "#3f0e40", "#350d36", "#4a154b", "#f8f8f8", "#b49bb5", "#ecb22e", "#36c5f0")
add("spotify", "meme", "dark", "#121212", "#181818", "#282828", "#ffffff", "#b3b3b3", "#1db954", "#1ed760")
add("doom", "meme", "dark", "#1a0000", "#2a0505", "#3a0a0a", "#ffcc66", "#8b3a3a", "#ff4500", "#b22222")
add("rgb-gamer", "meme", "dark", "#0a0a0a", "#141414", "#1a1a1a", "#f0f0f0", "#666666", "#ff2d55", "#00e5ff")

# --- more pastel / nature ---------------------------------------------------------------------------
add("cotton-candy", "pastel", "light", "#fdf0f7", "#f7e0f0", "#ffffff", "#5a3a52", "#b088a5", "#f472b6", "#818cf8")
add("seafoam", "pastel", "light", "#effcf6", "#d6f5e8", "#ffffff", "#134e4a", "#5f9a8f", "#14b8a6", "#0ea5e9")
add("arctic", "nature", "light", "#f0f6fb", "#e2edf5", "#ffffff", "#1e3a4c", "#5f7d8f", "#0ea5e9", "#38bdf8")
add("desert", "nature", "light", "#faf3e8", "#f0e2ca", "#ffffff", "#4a3520", "#8a7050", "#d97706", "#65a30d")
add("cherry-blossom", "nature", "light", "#fff5f8", "#ffe4ec", "#ffffff", "#6a2540", "#b57a92", "#ec4899", "#a78bfa")


# One template of CSS rules; every palette flows through it. {v} placeholders are the palette vars.
TEMPLATE = """/* {name} ({category}, {mode}) - generated by web_assets/wizard/gen_themes.py from a palette.
   Opt a page in with <!--#theme generated/{name}-->. Hand-edit the palette in gen_themes.py, not here. */
:root {{
  --bg: {bg};
  --bg2: {bg2};
  --card: {card};
  --ink: {ink};
  --muted: {muted};
  --accent: {accent};
  --accent2: {accent2};
  --radius: {radius};
}}
* {{ box-sizing: border-box; }}
html, body {{
  margin: 0;
  min-height: 100%;
  color: var(--ink);
  font: 16px/1.6 {font};
  background: linear-gradient(135deg, var(--bg), var(--bg2)) fixed;
}}
main, .wrap {{ max-width: 880px; margin: 0 auto; padding: 24px; }}
h1, h2, h3 {{ color: var(--accent); font-weight: 700; line-height: 1.25; }}
a {{ color: var(--accent2); text-decoration: none; }}
a:hover {{ text-decoration: underline; }}
.muted, small {{ color: var(--muted); }}
.card, .panel {{
  background: var(--card);
  border: 1px solid color-mix(in srgb, var(--muted) 35%, transparent);
  border-radius: var(--radius);
  padding: 18px 20px;
  margin: 16px 0;
  box-shadow: {shadow};
}}
button, .btn {{
  background: var(--accent);
  color: {on_accent};
  border: 0;
  border-radius: var(--radius);
  padding: 10px 18px;
  font: inherit;
  font-weight: 600;
  cursor: pointer;
}}
button:hover, .btn:hover {{ filter: brightness(1.08); }}
button.secondary {{ background: var(--accent2); }}
input, select, textarea {{
  width: 100%;
  background: {input_bg};
  color: var(--ink);
  border: 1px solid color-mix(in srgb, var(--muted) 45%, transparent);
  border-radius: var(--radius);
  padding: 10px 12px;
  font: inherit;
}}
input:focus, select:focus, textarea:focus {{ outline: 2px solid var(--accent); border-color: var(--accent); }}
table {{ width: 100%; border-collapse: collapse; }}
th, td {{ text-align: left; padding: 8px 10px; border-bottom: 1px solid color-mix(in srgb, var(--muted) 30%, transparent); }}
th {{ color: var(--accent); }}
code, pre {{
  font-family: ui-monospace, "SF Mono", "Cascadia Code", Consolas, monospace;
  background: {code_bg};
  border-radius: calc(var(--radius) * 0.6);
}}
code {{ padding: 2px 6px; }}
pre {{ padding: 14px 16px; overflow-x: auto; }}
"""

# Per-category shaping (radius / shadow / font). Enterprise = restrained; meme = loud.
STYLE = {
    "enterprise": {
        "radius": "8px",
        "font": '15px/1.6 "Segoe UI", system-ui, sans-serif',
        "shadow": "0 1px 3px rgba(0,0,0,.08)",
    },
    "editor": {
        "radius": "6px",
        "font": "15px/1.6 ui-sans-serif, system-ui, sans-serif",
        "shadow": "0 2px 8px rgba(0,0,0,.18)",
    },
    "retro": {
        "radius": "0",
        "font": '15px/1.55 ui-monospace, "Cascadia Code", monospace',
        "shadow": "0 0 0 1px currentColor inset",
    },
    "meme": {
        "radius": "18px",
        "font": '16px/1.6 "Comic Sans MS", "Segoe UI", sans-serif',
        "shadow": "0 6px 20px rgba(0,0,0,.25)",
    },
    "pastel": {
        "radius": "16px",
        "font": '16px/1.7 "Nunito", system-ui, sans-serif',
        "shadow": "0 4px 14px rgba(0,0,0,.08)",
    },
    "nature": {
        "radius": "12px",
        "font": '16px/1.65 "Georgia", "Segoe UI", serif',
        "shadow": "0 3px 12px rgba(0,0,0,.12)",
    },
}


def luminance(hexc):
    r, g, b = (int(hexc[i : i + 2], 16) / 255 for i in (1, 3, 5))
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


# --- palette derivation: the picky-user tool (give it your one exact color, get a whole theme) -------
import colorsys


def _rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i : i + 2], 16) / 255 for i in (0, 2, 4))


def _hex(r, g, b):
    return "#%02x%02x%02x" % tuple(max(0, min(255, round(c * 255))) for c in (r, g, b))


def _shift(hexc, dl=0.0, ds=0.0, dh=0.0):
    """Nudge a color in HLS space (lightness / saturation / hue), clamped."""
    r, g, b = _rgb(hexc)
    h, l, s = colorsys.rgb_to_hls(r, g, b)
    return _hex(*colorsys.hls_to_rgb((h + dh) % 1.0, max(0.0, min(1.0, l + dl)), max(0.0, min(1.0, s + ds))))


def derive_palette(base, mode, accent=None):
    """Build a full 7-color palette from one anchor color + a light/dark mode (and an optional accent).

    `base` is treated as the panel/surface color - the color the picky user actually cares about
    (their charcoal gray). Everything else is derived so contrast and the accent read well."""
    if accent:
        acc = accent
    else:
        h, l, s = colorsys.rgb_to_hls(*_rgb(base))
        # A near-gray base has no meaningful hue to complement, so give it a friendly blue; otherwise
        # take a vivid complementary at a fixed, readable lightness/saturation (not a muddy shift).
        acc = "#3b82f6" if s < 0.12 else _hex(*colorsys.hls_to_rgb((h + 0.5) % 1.0, 0.58, 0.72))
    acc2 = _shift(acc, dh=0.09, ds=0.05)
    if mode == "dark":
        return (
            _shift(base, dl=-0.06),
            base,
            _shift(base, dl=0.05),
            "#e8eaed",
            _shift(base, dl=0.32, ds=-0.15),
            acc,
            acc2,
        )
    return (
        _shift(base, dl=0.05),
        _shift(base, dl=-0.02),
        "#ffffff",
        "#171a1f",
        _shift(base, dl=-0.28, ds=-0.1),
        acc,
        acc2,
    )


def render_from(name, category, mode, bg, bg2, card, ink, muted, accent, accent2):
    st = STYLE[category]
    on_accent = "#0b0b0b" if luminance(accent) > 0.6 else "#ffffff"
    input_bg = "rgba(255,255,255,.06)" if mode == "dark" else "#ffffff"
    code_bg = "rgba(255,255,255,.08)" if mode == "dark" else "color-mix(in srgb, var(--muted) 14%, transparent)"
    return TEMPLATE.format(
        name=name,
        category=category,
        mode=mode,
        bg=bg,
        bg2=bg2,
        card=card,
        ink=ink,
        muted=muted,
        accent=accent,
        accent2=accent2,
        radius=st["radius"],
        font=st["font"],
        shadow=st["shadow"],
        on_accent=on_accent,
        input_bg=input_bg,
        code_bg=code_bg,
    )


def render(name):
    return render_from(name, *P[name])


# The seven original bespoke themes (hand-authored, not palette-generated) - shown in the gallery too.
HAND_THEMES = ["bubbly", "crt-green", "cute", "keroppi", "nyancat", "rainbow", "ssh-terminal"]
GALLERY = os.path.join(REPO_ROOT, "docs", "THEMES.md")
CAT_ORDER = ["enterprise", "editor", "retro", "pastel", "nature", "meme"]
CAT_BLURB = {
    "enterprise": "Calm, boardroom-safe. Reach for these on a product.",
    "editor": "Recognizable code-editor schemes, matched to their canonical palettes.",
    "retro": "Terminals and 8-bit machines.",
    "pastel": "Soft and friendly.",
    "nature": "Earth, sea, and seasons.",
    "meme": "Loud and fun - for the kids (and the young at heart).",
}


def _grid(names):
    """A 3-column HTML table of theme previews with their names (renders on GitHub + Doxygen)."""
    out = ["<table>"]
    for i in range(0, len(names), 3):
        out.append("<tr>")
        for n in names[i : i + 3]:
            mark = " &dagger;" if n in RESTRICTED else ""  # trademark-named: open-source only
            out.append(
                f'<td align="center"><img src="theme_preview/{n}.png" width="240" alt="{n}"><br><code>{n}</code>{mark}</td>'
            )
        out.append("</tr>")
    out.append("</table>")
    return "\n".join(out)


def cmd_gallery():
    by_cat = {}
    for n, v in P.items():
        by_cat.setdefault(v[0], []).append(n)
    lines = [
        "# Themes",
        "",
        "> Gallery generated by `web_assets/wizard/gen_themes.py gallery`; previews by"
        " `ci_tooling/assets/render_theme_previews.cjs`. Do not edit by hand.",
        "",
        f"**{len(P) + len(HAND_THEMES)} themes.** Opt a served page into one with a build directive -"
        " `<!--#theme dracula-->` or `<!--#theme generated/dracula-->` before `</head>` (see"
        " [web_assets/README.md](../web_assets/README.md)). Need an exact color that is not here? Generate your"
        " own from one anchor color:",
        "",
        "```sh",
        "python web_assets/wizard/gen_themes.py custom --base '#36454f' --mode dark --name charcoal --out my.css",
        "```",
        "",
        "&dagger; = named after a company/product trademark: shipped in the open-source (AGPL) build, but"
        " dropped from a commercial build (`PC_THEMES_INCLUDE_TRADEMARKED=0`). The palette is just colors;"
        " only the branded name is gated.",
        "",
        "## Bespoke",
        "",
        "Hand-authored, with personality the palette engine cannot capture (CRT scanlines, nyan trails).",
        "",
        _grid(HAND_THEMES),
        "",
    ]
    for cat in CAT_ORDER:
        if by_cat.get(cat):
            lines += [f"## {cat.title()}", "", CAT_BLURB.get(cat, ""), "", _grid(sorted(by_cat[cat])), ""]
    with open(GALLERY, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines).rstrip("\n") + "\n")
    print(f"wrote {os.path.relpath(GALLERY)} ({len(P) + len(HAND_THEMES)} themes)")
    return 0


def _arg(flag, default=None):
    a = sys.argv[1:]
    return a[a.index(flag) + 1] if flag in a and a.index(flag) + 1 < len(a) else default


def main():
    argv = sys.argv[1:]
    # The picky-user tool: build a full theme around one exact color.
    #   gen_themes.py custom --base '#36454f' --mode dark --name charcoal [--accent '#00b4d8'] [--out file.css]
    if argv and argv[0] == "custom":
        base = _arg("--base")
        if not base:
            sys.exit("custom: --base '#rrggbb' is required (the one color you want the theme built around)")
        name = _arg("--name", "custom")
        mode = _arg("--mode", "dark")
        category = _arg("--category", "enterprise")
        css = render_from(name, category, mode, *derive_palette(base, mode, _arg("--accent")))
        out = _arg("--out")
        if out:
            with open(out, "w", encoding="utf-8", newline="\n") as f:
                f.write(css)
            print(f"wrote {out}")
        else:
            sys.stdout.write(css)
        return 0
    if argv and argv[0] == "gallery":
        return cmd_gallery()
    check = "--check" in argv
    if "--list" in argv:
        for n in sorted(P):
            print(f"{n} {P[n][0]} {P[n][1]}")
        return 0
    os.makedirs(OUT_DIR, exist_ok=True)
    stale = []
    for name in sorted(P):
        path = os.path.join(OUT_DIR, name + ".css")
        content = render(name)
        old = open(path, encoding="utf-8").read() if os.path.isfile(path) else None
        if old != content:
            stale.append(name)
            if not check:
                with open(path, "w", encoding="utf-8", newline="\n") as f:
                    f.write(content)
    if check:
        if stale:
            sys.stderr.write("STALE themes (run gen_themes.py): " + ", ".join(stale) + "\n")
            return 1
        print(f"{len(P)} themes in sync")
        return 0
    print(f"generated {len(P)} themes -> {os.path.relpath(OUT_DIR)}" + (f" ({len(stale)} changed)" if stale else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
