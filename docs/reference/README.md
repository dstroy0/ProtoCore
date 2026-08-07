# Espressif SoC datasheets (reference)

Authoritative hardware references for the per-variant defaults in
[`src/core_setup/board_profiles/`](../../src/core_setup/board_profiles/). The board profiles size the library's
static pools and gate hardware-crypto paths per die, so the numbers below (internal SRAM,
crypto accelerators, radios, PSRAM support) come straight from these datasheets and each
target's ESP-IDF `soc_caps.h`.

The datasheet PDFs are **not committed** (they are large binaries and this is a published
library - see [`.gitignore`](../../.gitignore)); the links below point at Espressif's online
copies. To pull them locally for offline reference:

```sh
base="https://www.espressif.com/sites/default/files/documentation"
mkdir -p docs/reference
for f in esp32 esp32-s2 esp32-s3 esp32-s31 esp32-c3 esp32-c5 esp32-c6 esp32-c61 esp32-h2 esp32-p4 esp8684; do
  curl -fL -o "docs/reference/${f}_datasheet_en.pdf" "$base/${f}_datasheet_en.pdf"
done
```

## Datasheets by die

Each die maps to one ESP-IDF target macro (`CONFIG_IDF_TARGET_*`), which is how
`board_profile.h` selects its chip profile.

| SoC                 | Target macro                 | Internal SRAM  | Datasheet                                                                                     |
| ------------------- | ---------------------------- | -------------- | --------------------------------------------------------------------------------------------- |
| ESP32 (classic)     | `CONFIG_IDF_TARGET_ESP32`    | 520 KB         | [PDF](https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf)     |
| ESP32-S2            | `CONFIG_IDF_TARGET_ESP32S2`  | 320 KB         | [PDF](https://www.espressif.com/sites/default/files/documentation/esp32-s2_datasheet_en.pdf)  |
| ESP32-S3            | `CONFIG_IDF_TARGET_ESP32S3`  | 512 KB         | [PDF](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf)  |
| ESP32-C2 (ESP8684)  | `CONFIG_IDF_TARGET_ESP32C2`  | 272 KB         | [PDF](https://www.espressif.com/sites/default/files/documentation/esp8684_datasheet_en.pdf)   |
| ESP32-C3            | `CONFIG_IDF_TARGET_ESP32C3`  | 400 KB         | [PDF](https://www.espressif.com/sites/default/files/documentation/esp32-c3_datasheet_en.pdf)  |
| ESP32-C5            | `CONFIG_IDF_TARGET_ESP32C5`  | 384 KB         | [PDF](https://www.espressif.com/sites/default/files/documentation/esp32-c5_datasheet_en.pdf)  |
| ESP32-C6            | `CONFIG_IDF_TARGET_ESP32C6`  | 512 KB         | [PDF](https://www.espressif.com/sites/default/files/documentation/esp32-c6_datasheet_en.pdf)  |
| ESP32-C61           | `CONFIG_IDF_TARGET_ESP32C61` | 320 KB         | [PDF](https://www.espressif.com/sites/default/files/documentation/esp32-c61_datasheet_en.pdf) |
| ESP32-H2            | `CONFIG_IDF_TARGET_ESP32H2`  | 320 KB         | [PDF](https://www.espressif.com/sites/default/files/documentation/esp32-h2_datasheet_en.pdf)  |
| ESP32-P4            | `CONFIG_IDF_TARGET_ESP32P4`  | 768 KB (L2MEM) | [PDF](https://www.espressif.com/sites/default/files/documentation/esp32-p4_datasheet_en.pdf)  |
| ESP32-S31 (preview) | `CONFIG_IDF_TARGET_ESP32S31` | 512 KB         | [PDF](https://www.espressif.com/sites/default/files/documentation/esp32-s31_datasheet_en.pdf) |

Preview parts whose datasheet PDF is not published yet (target exists in ESP-IDF `master`
only, gated in the profiles): **ESP32-H4**, **ESP32-H21**. **ESP32-E22** is a Wi-Fi 6E
connectivity co-processor, not a standalone application target (no `CONFIG_IDF_TARGET`).

Hardware design guidelines were retired as standalone PDFs; they now live online at
<https://docs.espressif.com/projects/esp-hardware-design-guidelines/>.

Lineup: <https://www.espressif.com/en/products/socs>.
