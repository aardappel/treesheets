#!/usr/bin/env python3
"""Client for TreeSheets' local agent socket.

TreeSheets (this repo), when launched with -a, listens on a token-authenticated
local socket and runs Lobster script against whatever document is currently
open, returning the result. See ../SKILL.md for the full protocol and usage
notes. This script needs no setup beyond TreeSheets already running with -a:
it finds the endpoint and reads its per-launch token itself.

Endpoints (the token is always in "<endpoint>.token"):
  - macOS/Linux: Unix domain socket /tmp/TreeSheets-agent-<user>-<pid>.sock
  - Windows: %TEMP%\\TreeSheets-agent-<user>-<pid>.port, a file holding the TCP
    port TreeSheets listens on at 127.0.0.1
  - Windows build under Wine, client on the Linux host: that same .port file
    inside the Wine prefix, found automatically if no native socket exists
Builds from before the PID was added use the same names without "-<pid>"; those
are found too. If more than one instance is reachable, pick one with --pid or
--endpoint.
"""
import argparse
import getpass
import glob
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile


def current_user():
    try:
        return getpass.getuser()
    except Exception:
        return "unknown"


# Optional "-<pid>": builds from before the PID was added leave it out.
ENDPOINT_RE = re.compile(r"^TreeSheets-agent-(?P<user>.+?)(?:-(?P<pid>\d+))?\.(?:sock|port)$")


def endpoint_pid(endpoint):
    m = ENDPOINT_RE.match(os.path.basename(endpoint))
    return int(m["pid"]) if m and m["pid"] else None


def native_endpoints():
    if sys.platform == "win32":
        pattern = os.path.join(tempfile.gettempdir(), "TreeSheets-agent-*.port")
    else:
        pattern = "/tmp/TreeSheets-agent-*.sock"
    user = current_user()
    return [p for p in glob.glob(pattern)
            if (m := ENDPOINT_RE.match(os.path.basename(p))) and m["user"] == user]


def wine_endpoints():
    """.port files of Windows TreeSheets instances running under Wine."""
    prefixes = [os.environ.get("WINEPREFIX"), os.path.expanduser("~/.wine")]
    found = set()
    for prefix in filter(None, prefixes):
        for temp in ("AppData/Local/Temp", "Temp", "Local Settings/Temp"):
            pattern = os.path.join(prefix, "drive_c/users/*", temp, "TreeSheets-agent-*.port")
            found.update(p for p in glob.glob(pattern) if ENDPOINT_RE.match(os.path.basename(p)))
    return list(found)


def is_live(endpoint):
    """Whether something accepts connections there. Files left behind by a killed
    instance refuse the connection, so this filters those out on every platform,
    including Wine, whose Windows PIDs can't be checked from the host."""
    try:
        open_socket(endpoint, 2.0).close()
        return True
    except (OSError, ValueError):
        return False


def find_endpoint(pid):
    for candidates in (native_endpoints(), wine_endpoints()):
        if pid is not None:
            candidates = [p for p in candidates if endpoint_pid(p) == pid]
        live = sorted((p for p in candidates if is_live(p)), key=os.path.getmtime, reverse=True)
        if len(live) == 1:
            return live[0]
        if len(live) > 1:
            lines = "\n".join(f"  pid {endpoint_pid(p) or '?'}: {p}" for p in live)
            raise SystemExit(
                f"error: {len(live)} TreeSheets agent endpoints are reachable:\n{lines}\n"
                f"Pick one with --pid or --endpoint."
            )
    which = f" with pid {pid}" if pid is not None else ""
    raise SystemExit(
        f"error: no reachable TreeSheets agent endpoint{which} found.\n"
        f"Is TreeSheets running with -a (agent mode)?"
    )


def is_tcp(endpoint):
    return endpoint.endswith(".port")


def is_wine(endpoint):
    return is_tcp(endpoint) and sys.platform != "win32"


def read_token(endpoint):
    token_path = endpoint + ".token"
    try:
        with open(token_path) as f:
            return f.read().strip()
    except OSError as e:
        raise SystemExit(
            f"error: can't read token file {token_path}: {e}\n"
            f"Is TreeSheets running with -a (agent mode)?"
        )


def open_socket(endpoint, timeout):
    if is_tcp(endpoint):
        with open(endpoint) as f:
            port = int(f.read().strip())
        return socket.create_connection(("127.0.0.1", port), timeout=timeout)
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    try:
        s.settimeout(timeout)
        s.connect(endpoint)
    except OSError:
        s.close()
        raise
    return s


def connect(endpoint, timeout):
    try:
        return open_socket(endpoint, timeout)
    except (OSError, ValueError) as e:
        raise SystemExit(
            f"error: can't connect via {endpoint}: {e}\n"
            f"Is TreeSheets running with -a (agent mode)?"
        )


def to_wine_path(path):
    """TreeSheets under Wine opens files by Windows path; map a host path onto one."""
    path = os.path.abspath(path)
    if shutil.which("winepath"):
        try:
            out = subprocess.run(["winepath", "-w", path], capture_output=True, text=True,
                                 timeout=30, check=True).stdout.strip()
            if out:
                return out
        except (OSError, subprocess.SubprocessError):
            pass
    return "Z:" + path.replace("/", "\\")  # Wine's default mapping of the host root


def send(endpoint, token, req, timeout):
    req = dict(req)
    req["token"] = token
    req.setdefault("id", "1")

    s = connect(endpoint, timeout)

    f = s.makefile("rwb", buffering=0)
    f.write((json.dumps(req) + "\n").encode("utf-8"))
    line = f.readline()
    if not line:
        raise SystemExit("error: connection closed with no response")
    return json.loads(line.decode("utf-8"))


def cmd_ping(args):
    resp = send(args.endpoint, read_token(args.endpoint), {"cmd": "ping"}, args.timeout)
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
        req["file"] = to_wine_path(args.file) if is_wine(args.endpoint) else args.file
    elif args.stdin:
        req["code"] = sys.stdin.read()
    else:
        raise SystemExit("error: eval needs --code, --file, or --stdin")

    resp = send(args.endpoint, read_token(args.endpoint), req, args.timeout)
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
    # A shared parent so --endpoint/--timeout/--json work both before and after the
    # subcommand (argparse doesn't forward parent-only options placed after it).
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument(
        "--endpoint",
        "--socket",
        default=None,
        help="the agent's Unix socket, or on Windows its .port file "
        "(default: found automatically, see the module docstring)",
    )
    common.add_argument(
        "--pid",
        type=int,
        default=None,
        help="talk to the instance with this process ID (under Wine: its Windows PID)",
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
    if args.endpoint is None:
        args.endpoint = find_endpoint(args.pid)
    sys.exit(args.func(args))


if __name__ == "__main__":
    main()
