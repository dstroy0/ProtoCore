<!--
Thanks for contributing. Keep the PR focused on one logical change.
Use a Conventional Commit style title, e.g. "feat: add X" or "fix: handle Y".
-->

## Summary

<!-- What does this change and why? -->

## Related issues

<!-- e.g. Closes #123 -->

## Checklist

- [ ] One logical change; the title uses a Conventional Commit prefix.
- [ ] Added/updated a **native test** for new or changed logic, and it passes (`pio test -e <native_env>`).
- [ ] Compiles for hardware (`pio run -e esp32dev`).
- [ ] If a new feature: added a `PC_ENABLE_*` flag (default off), an example, and any dependency `#error` guard, and updated the README build-flag tree.
- [ ] No `<stdlib.h>` / `<cstdlib>` in library code; no runtime allocation in the request path.
- [ ] Follows the relevant RFC and cites it in code/docs (if applicable).
- [ ] Ran `clang-format` on changed C/C++/`.ino` and `npm run format` on Markdown.
- [ ] `npm run spell` (cspell) passes.
- [ ] Updated documentation affected by this change.

## Notes for reviewers

<!-- Anything tricky, trade-offs made, or areas you want a closer look at. -->
