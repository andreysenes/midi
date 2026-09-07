#!/usr/bin/env bash
# Sobe o Lab (frontend no browser) em http://127.0.0.1:8741
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
exec "$ROOT/lab/run.sh"
