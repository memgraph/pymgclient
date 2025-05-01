#!/usr/bin/env bash
set -euo pipefail

TIMEOUT=10
MEMGRAPH_CONSOLE_BINARY=${MEMGRAPH_CONSOLE_BINARY:-}

usage() {
  cat <<EOF
Usage: $0 [--timeout SECONDS] <host> [port]
  --timeout SECONDS   Max seconds to wait (default: $TIMEOUT)
  <host>              Memgraph host to check
  [port]              Memgraph port (default: 7687)
EOF
  exit 1
}

# --- parse flags ---
while [[ $# -gt 0 ]]; do
  case $1 in
    --timeout)
      [[ -n "${2-}" ]] || { echo "Error: --timeout needs an argument" >&2; usage; }
      TIMEOUT=$2
      shift 2
      ;;
    -h|--help)
      usage
      ;;
    *) 
      break
      ;;
  esac
done

# --- positional args ---
(( $# >= 1 && $# <= 2 )) || usage
HOST=$1
PORT=${2-7687}

# --- locate mgconsole ---
if [[ -z "$MEMGRAPH_CONSOLE_BINARY" ]]; then
  if command -v mgconsole &>/dev/null; then
    MEMGRAPH_CONSOLE_BINARY=$(command -v mgconsole)
    HAVE_MGCONSOLE=1
  else
    HAVE_MGCONSOLE=0
  fi
else
  if [[ ! -x "$MEMGRAPH_CONSOLE_BINARY" ]]; then
    echo "Error: \$MEMGRAPH_CONSOLE_BINARY set to '$MEMGRAPH_CONSOLE_BINARY', but not executable." >&2
    exit 1
  fi
  HAVE_MGCONSOLE=1
fi

# --- wait for a port on memgraph host with timeout ---
wait_port() {
  local host=$1 port=$2 timeout=$3 start now
  start=$(date +%s)
  while true; do
    if timeout 1 bash -c "(exec 3<>'/dev/tcp/${host}/${port}')" 2>/dev/null; then
      return 0
    fi
    now=$(date +%s)
    (( now - start >= timeout )) && {
      echo "Timeout ($timeout s) waiting for $host:$port" >&2
      return 1
    }
  done
}

# --- wait for memgraph console to respond with timeout ---
wait_for_memgraph() {
  local host=$1 port=$2 timeout=$3 start now
  start=$(date +%s)
  while true; do
    if timeout 1 bash -c "echo 'RETURN 1;' | \"$MEMGRAPH_CONSOLE_BINARY\" --host \"$host\" --port \"$port\" >/dev/null 2>&1"; then
      return 0
    fi
    now=$(date +%s)
    (( now - start >= timeout )) && {
      echo "Timeout ($timeout s) waiting for memgraph at $host:$port" >&2
      return 1
    }
    sleep 0.1
  done
}

# --- run checks ---
echo "Waiting for TCP port $HOST:$PORT (timeout ${TIMEOUT}s)..."
if wait_port "$HOST" "$PORT" "$TIMEOUT"; then
  timed_out=0
else
  timed_out=1
fi

if (( HAVE_MGCONSOLE )); then
  echo "Waiting for memgraph console on $HOST:$PORT (timeout ${TIMEOUT}s)..."
  wait_for_memgraph "$HOST" "$PORT" "$TIMEOUT"
else
  if [[ $timed_out == 1 ]]; then
    echo "mgconsole not found"
    exit 1
  fi
  echo "Note: mgconsole not found; skipping memgraph-console check."
fi

echo "Memgraph Started." 
