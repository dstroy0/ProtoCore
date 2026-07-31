# Web assets

User-configurable strings the firmware serves (HTML pages, the Prometheus
exposition, future CSS/JSON/XML) live here as editable source files instead of
being buried in `.cpp`. A single generator turns them into C string literals in
the application layer so they ship in flash with no filesystem or heap.

## Layout

| Path                                         | What                                                                  |
| -------------------------------------------- | --------------------------------------------------------------------- |
| `input/`                                     | One source document per symbol, named by the C identifier it backs.   |
| `themes/`                                    | Reusable CSS themes a document can pull in with a `#theme` directive. |
| `wizard/build_assets.py`                     | The generator.                                                        |
| `../network_drivers/application/web.{h,cpp}` | Generated output (committed; do not hand-edit).                       |

A file's **base name is the C symbol**: `PC_PROV_FORM.html` becomes
`extern const char PC_PROV_FORM[];`. Every document is emitted into the single
`application/web.{h,cpp}` translation unit - all generated assets are one
machine-produced artifact, so they live in one clearly-named unit rather than per
-content-type files (which would conflate source format with output module and
collide with real modules, e.g. the JSON codec at `presentation/json/json.h`). The
**extension only selects lint and transforms** (`.html`/`.svg`/`.xml` tag checks,
`.json` validation, etc.), not the output file.

## Workflow

```sh
# edit a document under input/, then regenerate:
python web_assets/wizard/build_assets.py            # (or: ... generate)

# CI / pre-commit gate - fails if the committed output is stale:
python web_assets/wizard/build_assets.py check
```

The generated files are committed (Arduino-IDE users do not run Python) and are
formatted to match clang-format, so re-running the generator produces no spurious
diff. Run the generator (and `clang-format`) after editing any source document.

## Directives (consumed, never served)

HTML/XML/SVG/text use `<!--#cmd arg-->`; CSS/JS use `/*#cmd arg*/`.

| Directive     | Effect                                                             |
| ------------- | ------------------------------------------------------------------ |
| `#brief TEXT` | becomes the Doxygen `@brief` on the generated declaration.         |
| `#theme NAME` | injects `themes/NAME.css` (minified) before `</head>` (HTML only). |
| `#minify`     | collapses markup whitespace (`<script>`/`<style>` preserved).      |

By default a document is emitted **byte-for-byte**, so the existing assets are
served exactly as before; the transforms above are opt-in.

## Themes

Drop-in CSS palettes under `themes/`, pulled into a page with `<!--#theme NAME-->`
(they share the same selectors, so they are interchangeable). Bundled:
`crt-green`, `ssh-terminal`, `bubbly`, `cute`, `rainbow`, `nyancat`, `keroppi`.
The built-in assets ship with their own inline styles and do not use a theme;
these are for pages you add. Drop a new `.css` in `themes/` to add one.

## Templates

A document may contain `{{name}}` placeholders rendered at request time by
`send_template()` (the value comes from a resolver in the C code, so there is no
`printf`-format coupling). The Prometheus exposition (`PC_METRICS_PROM.txt`)
works this way: it lists **every** available metric, and you disable one by
commenting its value line out with a leading `#` (Prometheus ignores `#` lines).
