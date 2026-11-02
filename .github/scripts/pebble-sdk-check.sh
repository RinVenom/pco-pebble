#!/usr/bin/env bash
# Called by .github/workflows/pebble-sdk-check.yml. Needs GH_TOKEN, and
# BUILD_OUTCOME (success/failure) from the build step.
set -euo pipefail

owner="${GITHUB_REPOSITORY%%/*}"
run_url="$GITHUB_SERVER_URL/$GITHUB_REPOSITORY/actions/runs/$GITHUB_RUN_ID"
baseline="${BASELINE_OVERRIDE:-$(tr -d '[:space:]' < pco-services/.pebble-sdk-version)}"

# Newest SDK: what `pebble sdk install latest` installed, and the newest
# changelog the developer site lists. Use whichever is higher.
installed="$(pebble sdk list 2>/dev/null | sed -n 's/^\([0-9][0-9.]*\) (active).*/\1/p' | head -1)"
changelogs="$(curl -fsS https://developer.repebble.com/llms.txt | grep -o 'Pebble SDK [0-9][0-9.]* - Changelog' | awk '{print $3}' || true)"
newest_log="$(printf '%s\n' $changelogs | sort -V | tail -1)"
newest="$(printf '%s\n%s\n' "$installed" "$newest_log" | grep . | sort -V | tail -1 || true)"
echo "App built for: $baseline | installed latest: ${installed:-?} | newest changelog: ${newest_log:-?}"

newer() { [ "$1" != "$2" ] && [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -1)" = "$1" ]; }

ensure_label() {
  gh label create pebble-sdk --color 1D76DB --description "Pebble SDK changes to check" >/dev/null 2>&1 || true
}

# Opens an issue unless one with this exact title exists. With "any", a
# closed issue also counts (an SDK release is reported once).
open_issue() {
  local title="$1" body="$2" state="${3:-open}"
  if [ -n "${DRY_RUN:-}" ]; then
    printf 'DRY RUN: would open issue "%s"\n---\n%s\n---\n' "$title" "$body"; return
  fi
  local found
  found="$(gh issue list --state "$state" --limit 200 --json title --jq '.[].title' | grep -Fxc -- "$title" || true)"
  if [ "$found" != "0" ]; then echo "Issue already exists: $title"; return; fi
  ensure_label
  gh issue create --title "$title" --label pebble-sdk --body "$body"
}

if [ -z "$changelogs" ]; then
  open_issue "Weekly Pebble SDK check could not read the changelog list" \
"The check looks for \"Pebble SDK x.y - Changelog\" in https://developer.repebble.com/llms.txt and found none. The page may have changed format; the check needs a fix.

Run: $run_url

@$owner"
fi

if [ -n "$newest" ] && newer "$newest" "$baseline"; then
  open_issue "Pebble SDK $newest released" \
"Pebble published SDK **$newest**. The app was last updated for SDK $baseline.

- Changelog: https://developer.repebble.com/sdk/changelogs/$newest.md
- Build with SDK ${installed:-unknown}: **${BUILD_OUTCOME}**
- Run: $run_url

To update: ask Claude Code to \"check the new Pebble SDK changes\" for this project. When the app is updated, set \`pco-services/.pebble-sdk-version\` to $newest and close this issue.

@$owner" any
fi

if [ "${BUILD_OUTCOME:-success}" != "success" ]; then
  log="$(tail -n 40 "$RUNNER_TEMP/build.log" 2>/dev/null || echo 'No build log.')"
  open_issue "App does not build with Pebble SDK ${installed:-latest}" \
"\`pebble build\` failed with the newest SDK (${installed:-unknown}).

Last lines of the build log:
\`\`\`
$log
\`\`\`

Full log: $run_url

@$owner"
fi
echo "Done."
