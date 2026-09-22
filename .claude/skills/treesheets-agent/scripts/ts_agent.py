#!/usr/bin/env python3
"""Client for TreeSheets' local agent socket.

TreeSheets (this repo), when launched with -a, listens on a token-authenticated
Unix domain socket and runs Lobster script against whatever document is
currently open, returning the result. See ../SKILL.md for the full protocol
and usage notes. This script needs no setup beyond TreeSheets already running
with -a: it finds the socket and reads its per-launch token itself.
"""
import argparse
import getpass
import json
import os
import socket
import sys


def default_socket_path():
    user = os.environ.get("USER") or os.environ.get("LOGNAME")
    if not user:
        try:
            user = getpass.getuser()
        except Exception:
            user = "unknown"
    return f"/tmp/TreeSheets-agent-{user}.sock"


def read_token(socket_path):
    token_path = socket_path + ".token"
    try:
        with open(token_path) as f:
            return f.read().strip()
    except OSError as e:
        raise SystemExit(
            f"error: can't read token file {token_path}: {e}\n"
            f"Is TreeSheets running with -a (agent mode)?"
        )


def send(socket_path, token, req, timeout):
    req = dict(req)
    req["token"] = token
    req.setdefault("id", "1")

    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(timeout)
    try:
        s.connect(socket_path)
    except OSError as e:
        raise SystemExit(
            f"error: can't connect to {socket_path}: {e}\n"
            f"Is TreeSheets running with -a (agent mode)?"
        )

    f = s.makefile("rwb", buffering=0)
    f.write((json.dumps(req) + "\n").encode("utf-8"))
    line = f.readline()
    if not line:
        raise SystemExit("error: connection closed with no response")
    return json.loads(line.decode("utf-8"))


def cmd_ping(args):
    resp = send(args.socket, read_token(args.socket), {"cmd": "ping"}, args.timeout)
    if args.json:
        print(json.dumps(resp))
    else:
        print("ok" if resp.get("ok") else f"error: {resp.get('error')}")
    return 0 if resp.get("ok") else 1


def cmd_eval(args):
    req = {"cmd": "eval"}
    if args.code is not None:
        req["code"] = args.code
    elif args.file is not None:
        req["file"] = args.file
    elif args.stdin:
        req["code"] = sys.stdin.read()
    else:
        raise SystemExit("error: eval needs --code, --file, or --stdin")

    resp = send(args.socket, read_token(args.socket), req, args.timeout)
    if args.json:
        print(json.dumps(resp))
    elif resp.get("ok"):
        print("ok")
        if resp.get("result"):
            print(f"result: {resp['result']}")
    else:
        print(f"error: {resp.get('error')}", file=sys.stderr)
    return 0 if resp.get("ok") else 1


def main():
    # A shared parent so --socket/--timeout/--json work both before and after the
    # subcommand (argparse doesn't forward parent-only options placed after it).
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument(
        "--socket",
        default=default_socket_path(),
        help="path to the agent socket (default: %(default)s)",
    )
    common.add_argument("--timeout", type=float, default=10.0, help="socket timeout in seconds")
    common.add_argument(
        "--json", action="store_true", help="print the raw JSON response instead of a summary"
    )

    p = argparse.ArgumentParser(description="Talk to a running TreeSheets agent socket.")
    sub = p.add_subparsers(dest="cmd", required=True)

    sp = sub.add_parser("ping", help="check that the agent server is reachable", parents=[common])
    sp.set_defaults(func=cmd_ping)

    sp = sub.add_parser(
        "eval",
        help="run Lobster code against the currently open document",
        parents=[common],
    )
    g = sp.add_mutually_exclusive_group(required=True)
    g.add_argument("--code", "-c", help="inline Lobster source")
    g.add_argument("--file", "-f", help="path to a .lobster file readable by TreeSheets")
    g.add_argument("--stdin", action="store_true", help="read inline source from stdin")
    sp.set_defaults(func=cmd_eval)

    args = p.parse_args()
    sys.exit(args.func(args))


if __name__ == "__main__":
    main()
