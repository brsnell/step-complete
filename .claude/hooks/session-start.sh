#!/bin/bash
# Installs gstack (github.com/garrytan/gstack), a set of Claude Code skills, in
# Claude Code cloud sessions. Local machines are left alone: install it there
# yourself (see the gstack section of CLAUDE.md).
set -euo pipefail

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
  exit 0
fi

gstack="$HOME/.claude/skills/gstack"
export PATH="$HOME/.bun/bin:$PATH"

# Appends a line to the session's env file unless it is already there.
add_env() {
  grep -qxF "$1" "$CLAUDE_ENV_FILE" 2>/dev/null || echo "$1" >> "$CLAUDE_ENV_FILE"
}

# Every session, before the install so a failed install cannot skip it: point
# gstack's browser at the Chromium preinstalled in cloud containers.
if [ -n "${CLAUDE_ENV_FILE:-}" ] && [ -x /opt/pw-browsers/chromium ]; then
  add_env 'export GSTACK_CHROMIUM_PATH=/opt/pw-browsers/chromium'
  add_env 'export GSTACK_CHROMIUM_NO_SANDBOX=1'
fi

# Run the install only if this script changed since the install last succeeded.
# The hash file lives in the install, so it disappears with it.
hashes="$gstack/.session-start.sha256"
script="${CLAUDE_PROJECT_DIR:-.}/.claude/hooks/session-start.sh"

# Steps are chained with && because set -e does not apply inside an if condition.
install_gstack() {
  { [ -d "$gstack/.git" ] ||
    git clone --quiet --single-branch --depth 1 https://github.com/garrytan/gstack.git "$gstack" >&2; } &&
    # --no-browser: setup's Chrome download is blocked in cloud sessions; the
    # container's own Chromium is used instead (above).
    (cd "$gstack" && ./setup --no-browser --no-plan-tune-hooks </dev/null >&2)
}

if ! (cd / && sha256sum --check --status "$hashes" 2>/dev/null); then
  rm -f "$hashes"
  # A failed install (e.g. github.com unreachable) must not fail the session start;
  # without the hash file, the next session tries again.
  if (install_gstack); then
    sha256sum "$(realpath "$script")" > "$hashes"
  else
    echo "session-start: gstack install failed; it will be retried next session" >&2
  fi
fi
