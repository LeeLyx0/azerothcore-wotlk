#!/usr/bin/env python3
import argparse
import json
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class Handler(BaseHTTPRequestHandler):
    mode = "ok"
    delay = 0.0

    def do_POST(self):
        length = int(self.headers.get("content-length", "0"))
        self.rfile.read(length)

        if self.delay:
            time.sleep(self.delay)

        if self.mode == "error":
            self.send_response(500)
            self.end_headers()
            self.wfile.write(b'{"error":"mock failure"}')
            return

        if self.mode == "invalid-json":
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b"{not json")
            return

        if self.mode == "ai":
            content = "As an AI language model, I cannot do that."
        elif self.mode == "oversized":
            content = " ".join(["Aye"] * 80)
        else:
            content = "Aye, I am still with you."

        body = {
            "choices": [
                {
                    "message": {
                        "role": "assistant",
                        "content": content,
                    }
                }
            ]
        }
        encoded = json.dumps(body).encode("utf-8")
        self.send_response(200)
        self.send_header("content-type", "application/json")
        self.send_header("content-length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def log_message(self, fmt, *args):
        return


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=11434)
    parser.add_argument(
        "--mode",
        choices=["ok", "error", "invalid-json", "ai", "oversized"],
        default="ok",
    )
    parser.add_argument("--delay", type=float, default=0.0)
    args = parser.parse_args()

    Handler.mode = args.mode
    Handler.delay = args.delay
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"mock OpenAI-compatible service on http://{args.host}:{args.port}")
    server.serve_forever()


if __name__ == "__main__":
    main()
