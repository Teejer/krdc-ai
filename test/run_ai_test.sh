#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 TJ Griffiths <teej.d.man@gmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Builds and runs the AI conversation engine test against a local mock
# OpenAI-compatible server.
#
#   ./test/run_ai_test.sh [builddir]
#
set -euo pipefail

src="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${1:-$src/build}"
port=8199
work="$(mktemp -d)"
trap 'kill 0 2>/dev/null || true; rm -rf "$work"' EXIT

command -v python3 >/dev/null || { echo "python3 is required" >&2; exit 1; }

cmake -S "$src" -B "$build" -DWITH_AI=YES -DBUILD_AI_ENGINE_TEST=YES >/dev/null
cmake --build "$build" --target aiengine_test -j"$(nproc)"

# The test reads its endpoint from krdcrc, so give it a throwaway config home.
mkdir -p "$work/config"
cat >"$work/config/krdcrc" <<EOF
[AiAssistant]
Endpoint=http://127.0.0.1:$port/v1
Model=test-model
AutoExecute=false
RememberHistory=true
EOF

cat >"$work/mock_ai.py" <<'PYEOF'
"""Mock OpenAI-compatible endpoint with scripted tool calls, for the engine test."""
import json
from http.server import BaseHTTPRequestHandler, HTTPServer


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0)) or b"{}"))
        messages = body.get("messages", [])
        user_text = " ".join(
            m["content"] if isinstance(m.get("content"), str) else json.dumps(m.get("content"))
            for m in messages if m.get("role") == "user"
        )
        done = any(m.get("role") == "tool" for m in messages)

        def send(payload):
            raw = json.dumps(payload).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)

        if done:
            results = [m.get("content") for m in messages if m.get("role") == "tool"]
            send({"choices": [{"message": {"role": "assistant",
                 "content": "Tool results came back: " + json.dumps(results)[:200]},
                 "finish_reason": "stop"}]})
            return

        if "rm -rf" in user_text:
            call = {"name": "type_text", "arguments": {"text": "sudo rm -rf /home/bob"}}
        elif "screenshot" in user_text.lower():
            call = {"name": "take_screenshot", "arguments": {}}
        elif "remember me" in user_text:
            send({"choices": [{"message": {"role": "assistant", "content": "Noted."},
                 "finish_reason": "stop"}]})
            return
        else:
            call = {"name": "mouse_click", "arguments": {"x": 400, "y": 300, "button": "left"}}

        send({"choices": [{"message": {"role": "assistant", "content": None,
              "tool_calls": [{"id": "call_1", "type": "function",
               "function": {"name": call["name"],
                            "arguments": json.dumps(call["arguments"])}}]},
              "finish_reason": "tool_calls"}]})

    def log_message(self, *args):
        pass


HTTPServer(("127.0.0.1", 8199), Handler).serve_forever()
PYEOF

python3 "$work/mock_ai.py" &
sleep 1

echo
echo "== running aiengine_test =="
XDG_CONFIG_HOME="$work/config" QT_QPA_PLATFORM=offscreen \
    "$build/bin/aiengine_test"
status=$?
exit $status
