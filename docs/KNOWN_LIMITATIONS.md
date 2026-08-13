# Known limitations

Deliberate constraints and caveats of ProtoCore. Most stem
from the zero-heap / fixed-buffer / single-loop design and are intentional; where
a limit is configurable, the relevant `*_SIZE` / `*_MAX` macro is named. Work to
lift some of these is tracked in [ROADMAP.md](ROADMAP.md).
