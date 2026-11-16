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

# Run the install only if this script changed since the install last succeeded.
# The hash file lives in the install, so it disappears with it.
hashes="$gstack/.session-start.sha256"
script="${CLAUDE_PROJECT_DIR:-.}/.claude/hooks/session-start.sh"
if ! (cd / && sha256sum --check --status "$hashes" 2>/dev/null); then
  rm -f "$hashes"
  if [ ! -d "$gstack/.git" ]; then
    git clone --quiet --single-branch --depth 1 https://github.com/garrytan/gstack.git "$gstack" >&2
  fi
  # --no-browser: setup's Chrome download is blocked in cloud sessions; the
  # container's own Chromium is used instead (below).
  (cd "$gstack" && ./setup --no-browser --no-plan-tune-hooks </dev/null >&2)
  sha256sum "$(realpath "$script")" > "$hashes"
fi

# Every session: point gstack's browser at the Chromium preinstalled in cloud containers.
if [ -n "${CLAUDE_ENV_FILE:-}" ] && [ -x /opt/pw-browsers/chromium ]; then
  echo 'export GSTACK_CHROMIUM_PATH=/opt/pw-browsers/chromium' >> "$CLAUDE_ENV_FILE"
  echo 'export GSTACK_CHROMIUM_NO_SANDBOX=1' >> "$CLAUDE_ENV_FILE"
fi
