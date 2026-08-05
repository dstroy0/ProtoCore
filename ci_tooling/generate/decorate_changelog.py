##
# @file decorate_changelog.py
# @brief Parses docs/CHANGELOG.md and wraps release version sections in collapsible detail cards.
# @details This script is run as part of the changelog generation pipeline to make release history collapsible,
#          maintaining a compact and navigable file.
# @author Douglas Quigg (dstroy0, dquigg123@gmail.com)
# @date June 2026
#

import os
import re
import sys

# Calculate project root dynamically (two levels up from docs/utilities)
PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
changelog_path = os.path.join(PROJECT_ROOT, "docs", "CHANGELOG.md")


##
# @brief Find all sections matching a heading and wrap their body text in a collapsible details element.
# @param content The raw text content of the markdown file.
# @param heading_regex The regular expression used to locate section headers.
# @param next_heading_regex The regular expression indicating the start of the next section.
# @param summary_text_builder A lambda/callback to construct the text of the details <summary> element.
# @return The updated markdown content with wrapped sections.
#
def wrap_all_between(content, heading_regex, next_heading_regex, summary_text_builder):
    matches = list(re.finditer(heading_regex, content))
    # Process in reverse order to keep offsets valid during string slices
    for i in range(len(matches) - 1, -1, -1):
        match = matches[i]
        start_idx = match.end()
        # Find the next heading (or end of file)
        next_match = re.search(next_heading_regex, content[start_idx:])
        if next_match:
            end_idx = start_idx + next_match.start()
        else:
            end_idx = len(content)

        heading_text = match.group(1) if match.groups() else match.group(0)
        summary_text = summary_text_builder(heading_text)

        body = content[start_idx:end_idx].strip()

        # Avoid double-wrapping the section
        if "<details>" in body:
            continue

        wrapped = f"\n\n<details>\n<summary><b>{summary_text}</b></summary>\n\n{body}\n\n</details>\n\n"
        content = content[:start_idx] + wrapped + content[end_idx:]
    return content


if __name__ == "__main__":
    if not os.path.exists(changelog_path):
        print(f"Error: {changelog_path} not found")
        sys.exit(1)

    with open(changelog_path, "r", encoding="utf-8") as f:
        content = f.read()

    # Wrap older releases, e.g. ## [1.2.0]... but not ## [Unreleased]
    updated = wrap_all_between(
        content,
        r"(?m)^## (\[\d+\.\d+\.\d+\][^#\n]*)$",
        r"(?m)^## ",
        lambda heading: f"Show Changelog for version {heading.replace('[', '').replace(']', '').strip()}",
    )

    # `--check` is the one convention across ci_tooling/ (see ci_tooling/README.md).
    # This had no check mode, so it could not be gated at all.
    if "--check" in sys.argv[1:]:
        if updated != content:
            print(f"STALE: {changelog_path} is not decorated; rerun decorate_changelog.py", file=sys.stderr)
            sys.exit(1)
        print(f"{changelog_path}: decoration up to date")
        sys.exit(0)

    with open(changelog_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(updated)
    print(f"Successfully decorated {changelog_path}")
