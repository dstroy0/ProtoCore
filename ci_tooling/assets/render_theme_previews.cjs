// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Render every theme in web_assets/themes/{,generated/} onto one small sample page and screenshot it to a
// PNG in docs/theme_preview/. The docs gallery (docs/THEMES.md, written by gen_themes.py gallery) embeds
// these so a browsing user - kid or pro - can see every theme at a glance before picking one. Needs
// puppeteer-core + a Chromium; run on a host that has both (CHROME overrides the browser path).
//
//   node ci_tooling/assets/render_theme_previews.cjs
const puppeteer = require(process.env.PUPPETEER || "/usr/local/lib/node_modules/puppeteer-core");
const fs = require("fs");
const path = require("path");

const ROOT = path.join(__dirname, "../..");
const DIRS = [path.join(ROOT, "web_assets/themes"), path.join(ROOT, "web_assets/themes/generated")];
const OUT = path.join(ROOT, "docs/theme_preview");

// A compact showcase touching the elements the template themes: heading, link, cards, buttons, an input,
// a small table, inline + block code. Uses the shared class vocabulary (.wrap/.card/.btn/.secondary).
const SAMPLE = `<main class="wrap">
  <h1>DeterministicESP</h1>
  <p class="muted">Theme preview &middot; <a href="#">the friendly ESP32 web server</a></p>
  <div class="card">
    <h2>Status</h2>
    <p>Uptime 4d 12h &middot; free heap <code>212 KB</code> &middot; 3 clients.</p>
    <p><button>Save</button> <button class="secondary">Reboot</button></p>
  </div>
  <div class="card">
    <h3>Sign in</h3>
    <p><input placeholder="username" value="admin"></p>
    <table>
      <tr><th>Route</th><th>Hits</th></tr>
      <tr><td>/api/status</td><td>1,204</td></tr>
      <tr><td>/ws</td><td>88</td></tr>
    </table>
  </div>
  <pre><code>server.on("/", [](Req&amp; r){ r.send(200, "text/html", page); });</code></pre>
</main>`;

(async () => {
  fs.mkdirSync(OUT, { recursive: true });
  const browser = await puppeteer.launch({
    executablePath: process.env.CHROME || "/usr/bin/chromium",
    args: ["--no-sandbox", "--disable-gpu", "--hide-scrollbars"],
  });
  const page = await browser.newPage();
  await page.setViewport({ width: 460, height: 380, deviceScaleFactor: 2 });
  let n = 0;
  for (const dir of DIRS) {
    if (!fs.existsSync(dir)) continue;
    for (const file of fs.readdirSync(dir).filter((f) => f.endsWith(".css")).sort()) {
      const name = file.replace(/\.css$/, "");
      const css = fs.readFileSync(path.join(dir, file), "utf8");
      const html = `<!doctype html><html><head><meta charset="utf-8"><style>${css}</style></head><body>${SAMPLE}</body></html>`;
      await page.setContent(html, { waitUntil: "domcontentloaded", timeout: 60000 }); // inline CSS, no network
      await new Promise((r) => setTimeout(r, 150)); // let layout + web-safe fonts settle
      await page.screenshot({ path: path.join(OUT, name + ".png") });
      n++;
    }
  }
  await browser.close();
  console.log(`rendered ${n} theme previews -> docs/theme_preview/`);
})().catch((e) => {
  console.error(e.message);
  process.exit(1);
});
