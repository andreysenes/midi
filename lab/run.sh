#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT/.."
if [[ ! -d lab/.venv ]]; then
  python3 -m venv lab/.venv
fi
# shellcheck disable=SC1091
source lab/.venv/bin/activate
python3 -m pip install -q -r lab/requirements.txt
exec python3 lab/server.py
