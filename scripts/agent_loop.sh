#!/usr/bin/env bash
# scripts/agent_loop.sh
#
# Decision helper for the auto-loop protocol in AGENTS.md §8.
# Both claude and deepseek invoke this at the top of each /loop iteration.
#
# Usage:  bash scripts/agent_loop.sh <self>
#   <self> = "claude" | "deepseek"
#
# Output (stdout): one of ACT | SLEEP_SHORT | SLEEP_LONG | STOP
# Diagnostics on stderr.

set -uo pipefail

SELF="${1:?usage: agent_loop.sh <claude|deepseek>}"

if [[ "$SELF" != "claude" && "$SELF" != "deepseek" ]]; then
    echo "ERROR: <self> must be 'claude' or 'deepseek', got '$SELF'" >&2
    exit 2
fi

# Repo root = parent of this script's dir.
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

# Define which commit prefixes belong to each agent.
case "$SELF" in
    claude)   MY_PREFIXES='^(test|review)\(claude\):'
              PEER_PREFIXES='^(impl|fix|wip|reflect)\(deepseek\):'
              FORBIDDEN_FOR_PEER='AGENTS\.md|REVIEW\.md|tests/integration/forensic_claude_test\.cpp'
              ;;
    deepseek) MY_PREFIXES='^(impl|fix|wip|reflect)\(deepseek\):'
              PEER_PREFIXES='^(test|review)\(claude\):'
              FORBIDDEN_FOR_PEER='REFLECTION\.md|src/|include/'
              ;;
esac

LAST_MSG="$(git log -1 --format=%s 2>/dev/null || echo '')"
LAST_HASH="$(git log -1 --format=%h 2>/dev/null || echo '')"
LAST_FILES="$(git diff-tree --no-commit-id --name-only -r HEAD 2>/dev/null || echo '')"

echo "[$SELF] latest commit: $LAST_HASH \"$LAST_MSG\"" >&2

# -----------------------------------------------------------------------------
# §8.3 STOP triggers
# -----------------------------------------------------------------------------

# (4) Protocol violation: peer touched files they don't own.
if [[ "$LAST_MSG" =~ $PEER_PREFIXES ]]; then
    if echo "$LAST_FILES" | grep -qE "$FORBIDDEN_FOR_PEER"; then
        echo "STOP: peer commit $LAST_HASH modified forbidden files:" >&2
        echo "$LAST_FILES" | grep -E "$FORBIDDEN_FOR_PEER" >&2
        echo "STOP"
        exit 0
    fi
fi

# (3) Budget cap: my commits since the last user-triggered commit.
USER_CUTOFF="$(git log --grep='^(test\|review\|impl\|fix\|wip\|reflect\|chore)' \
    --invert-grep --format=%h -1 2>/dev/null || true)"
if [[ -n "$USER_CUTOFF" ]]; then
    MY_COMMIT_COUNT="$(git log ${USER_CUTOFF}..HEAD --format=%s 2>/dev/null \
        | grep -cE "$MY_PREFIXES" || true)"
else
    MY_COMMIT_COUNT="$(git log --format=%s 2>/dev/null \
        | grep -cE "$MY_PREFIXES" || true)"
fi
if (( MY_COMMIT_COUNT >= 30 )); then
    echo "STOP: $SELF has made $MY_COMMIT_COUNT commits since last user commit (cap 30)" >&2
    echo "STOP"
    exit 0
fi

# (2) Death-loop guard: count "fail → fix → fail" iterations on the same
# pending item. Heuristic: same failing test name appearing in §14 log ≥ 3
# times paired with a deepseek commit each time.
PENDING_FILE="REVIEW.md"
if [[ -f "$PENDING_FILE" ]]; then
    REPEATED_FAILS="$(grep -oE 'ForensicClaude\.[A-Za-z]+' "$PENDING_FILE" 2>/dev/null \
        | sort | uniq -c | awk '$1 >= 3 {print $2}' | head -1)"
    if [[ -n "$REPEATED_FAILS" ]]; then
        echo "STOP: test $REPEATED_FAILS appears ≥3 times in REVIEW.md §14 (suspected death loop)" >&2
        echo "STOP"
        exit 0
    fi
fi

# (1) Job done: no pending items + forensic test all green + last commit is mine.
PENDING_COUNT="$(awk '/^## 6\./{flag=1; next} /^## 7\./{flag=0} flag' AGENTS.md 2>/dev/null \
    | grep -cE '^- \*\*\[' || true)"
STRUCK_COUNT="$(awk '/^## 6\./{flag=1; next} /^## 7\./{flag=0} flag' AGENTS.md 2>/dev/null \
    | grep -cE '^- ~~' || true)"
ACTIVE_PENDING=$(( PENDING_COUNT - STRUCK_COUNT ))

if (( ACTIVE_PENDING <= 0 )) && [[ "$LAST_MSG" =~ $MY_PREFIXES ]]; then
    # Verify forensic test is green.
    if bazel test //:forensic_claude_test --test_output=errors >/dev/null 2>&1; then
        echo "STOP: no pending work + forensic_claude_test green + last commit is mine" >&2
        echo "STOP"
        exit 0
    fi
fi

# -----------------------------------------------------------------------------
# §8.1 ACT / SLEEP_SHORT / SLEEP_LONG
# -----------------------------------------------------------------------------

if [[ "$LAST_MSG" =~ $PEER_PREFIXES ]]; then
    echo "[$SELF] peer just committed → ACT" >&2
    echo "ACT"
    exit 0
fi

if [[ "$LAST_MSG" =~ $MY_PREFIXES ]]; then
    echo "[$SELF] my own last commit, peer hasn't responded → SLEEP_SHORT" >&2
    echo "SLEEP_SHORT"
    exit 0
fi

# Last commit is neither's (user / chore). Check pending queue.
if (( ACTIVE_PENDING > 0 )); then
    echo "[$SELF] pending queue has $ACTIVE_PENDING active item(s) → ACT" >&2
    echo "ACT"
    exit 0
fi

echo "[$SELF] nothing to do, last commit not ours, no pending → SLEEP_LONG" >&2
echo "SLEEP_LONG"
