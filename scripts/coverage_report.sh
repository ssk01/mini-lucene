#!/usr/bin/env bash
# Renders a line-coverage summary for src/ headers after running
#   bazel coverage --config=coverage //...
#
# Requires clang/llvm-cov on PATH (macOS: `brew install llvm`, then
# add /usr/local/opt/llvm/bin or /opt/homebrew/opt/llvm/bin to PATH).
#
# Usage:
#   bash scripts/coverage_report.sh
#   bash scripts/coverage_report.sh --html out_dir   # also emit HTML

set -euo pipefail

cd "$(dirname "$0")/.."

# Bazel's --combined_report=lcov produces bazel-out/_coverage/_coverage_report.dat
# (under the actual output_path symlink target).
LCOV="$(bazel info output_path 2>/dev/null)/_coverage/_coverage_report.dat"
if [[ ! -f "$LCOV" ]]; then
    echo "no coverage data at $LCOV" >&2
    echo "hint: bazel coverage --config=coverage //... --combined_report=lcov" >&2
    exit 1
fi
echo "=== combined LCOV summary ==="
awk -F: '
        $1=="SF" { file=$2 }
        $1=="LF" { total[file]+=$2 }
        $1=="LH" { hit[file]+=$2 }
        END {
            grand_t=0; grand_h=0
            for (f in total) {
                pct = total[f] ? 100.0*hit[f]/total[f] : 0
                if (f ~ /(^|\/)(src|include)\//) {
                    printf "  %5.1f%%  %5d/%-5d  %s\n", pct, hit[f], total[f], f
                    grand_t += total[f]; grand_h += hit[f]
                }
            }
            if (grand_t) printf "  ---\n  TOTAL  %5.1f%%  %d/%d lines (src/+include/)\n", \
                100.0*grand_h/grand_t, grand_h, grand_t
        }
    ' "$LCOV" | sort -nr

if [[ "${1:-}" == "--html" && -n "${2:-}" ]]; then
    if command -v genhtml >/dev/null 2>&1; then
        genhtml --quiet --output-directory "$2" "$LCOV"
        echo "HTML coverage report: $2/index.html"
    else
        echo "genhtml not found; install lcov: brew install lcov" >&2
    fi
fi
