#!/usr/bin/env bash
# Merge open, mergeable Dependabot PRs into the current branch. Invoked by the post-commit hook.
#
# Strictly best-effort and non-blocking. It does nothing (and never fails the caller) when:
#   - gh is not installed or not authenticated (offline / no credentials),
#   - HEAD is detached, or a rebase / merge / cherry-pick / bisect is in progress,
#   - the current branch is not the base branch of any open Dependabot PR.
# Each candidate PR is merged only if GitHub reports it MERGEABLE and it targets the current branch;
# a merge that does not apply cleanly is aborted, leaving the working tree untouched. The operation
# is idempotent: a PR already contained in the branch merges as an "Already up to date" no-op, so
# repeated commits do not pile up duplicate merge commits.
set -uo pipefail
cd "$(git rev-parse --show-toplevel)" 2>/dev/null || exit 0

# Re-entrancy guard: the git merge below must not recurse back into this script.
[ -n "${DEPBOT_MERGE_RUNNING:-}" ] && exit 0
export DEPBOT_MERGE_RUNNING=1

# Need an authenticated GitHub CLI; skip silently otherwise so commits stay snappy and offline-safe.
command -v gh >/dev/null 2>&1 || exit 0
gh auth status >/dev/null 2>&1 || exit 0

# Only from a settled branch checkout - never mid history-editing or on a detached HEAD.
branch=$(git symbolic-ref --quiet --short HEAD) || exit 0
git_dir=$(git rev-parse --git-dir 2>/dev/null) || exit 0
for m in MERGE_HEAD CHERRY_PICK_HEAD REVERT_HEAD BISECT_LOG rebase-merge rebase-apply; do
    [ -e "$git_dir/$m" ] && exit 0
done

# Open Dependabot PRs targeting this branch that GitHub currently reports MERGEABLE
# (mergeable is computed asynchronously; UNKNOWN / CONFLICTING are left alone).
list=$(gh pr list --author 'app/dependabot' --state open --base "$branch" \
    --json number,headRefName,mergeable,title \
    --jq '.[] | select(.mergeable=="MERGEABLE") | [.number, .headRefName, .title] | @tsv' 2>/dev/null) || exit 0
[ -n "$list" ] || exit 0

printf '%s\n' "$list" | while IFS=$'\t' read -r num head title; do
    [ -n "$num" ] && [ -n "$head" ] || continue
    # Fetch the PR's head branch; skip it on any failure (deleted branch, transient network error).
    if ! git fetch --quiet origin "$head" 2>/dev/null; then
        echo "post-commit: Dependabot #$num skipped (fetch failed)"
        continue
    fi
    # Merge it; on a conflict abort so the branch is left exactly as it was before the attempt.
    if out=$(git merge --no-ff --no-edit -m "Merge Dependabot #$num: $title" FETCH_HEAD 2>&1); then
        case "$out" in
        *"Already up to date"*) echo "post-commit: Dependabot #$num already integrated" ;;
        *) echo "post-commit: merged Dependabot #$num ($title)" ;;
        esac
    else
        git merge --abort >/dev/null 2>&1 || true
        echo "post-commit: Dependabot #$num skipped (does not merge cleanly)"
    fi
done
exit 0
