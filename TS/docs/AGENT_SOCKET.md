# Using TreeSheets with an AI agent

Starting TreeSheets with the `-a` flag opens a local, token-authenticated
socket that lets an external agent run [Lobster](https://strlen.com/lobster/)
script against whatever document you currently have open, and read the
result back — useful if you want an AI coding assistant to inspect,
automate, or build up a TreeSheets document on your behalf.

```sh
TreeSheets -a
```

It's off by default, only listens locally, and the socket is authenticated
with a random, per-launch token file readable only by you. It's available on
macOS, Linux and Windows:

- **macOS and Linux:** a Unix domain socket, `/tmp/TreeSheets-agent-<user>-<pid>.sock`.
- **Windows:** a TCP socket on `127.0.0.1`, on a port chosen at each launch
  and written to `%TEMP%\TreeSheets-agent-<user>-<pid>.port`. This also works for
  the Windows build running under Wine on Linux.

In both cases the token is in the same path with `.token` appended. If `-a`
doesn't produce a socket (or `.port` file), your TreeSheets build predates
this feature.

If you're using [Claude Code](https://claude.com/claude-code) in this
repository, it can drive a running TreeSheets directly through the bundled
`treesheets-agent` skill (`.claude/skills/treesheets-agent`) — just ask it to
inspect or script your open document once TreeSheets is running with `-a`.
The skill's `SKILL.md` also documents the underlying wire protocol (a small
newline-delimited JSON format) and works whether or not you have this
repository checked out, so other agents or tools can talk to the socket
directly without any TreeSheets-specific client library.
