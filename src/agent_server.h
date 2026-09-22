// Lets an external agent (e.g. an AI coding assistant) run Lobster script against the
// currently open document over a local socket. Opt-in via -a / --agent.
//
// Wire protocol: newline-delimited JSON, one request per line in, one response per line out.
// Only a flat, all-string-valued object shape is needed, so parsing/building it is hand-rolled
// here rather than pulling in a JSON library.
//
//   -> {"id":"1","token":"<token>","cmd":"ping"}
//   <- {"id":"1","ok":true,"error":"","result":"pong"}
//
//   -> {"id":"2","token":"<token>","cmd":"eval","code":"print 1 + 1"}
//   <- {"id":"2","ok":true,"error":"","result":""}
//
//   -> {"id":"3","token":"<token>","cmd":"eval","file":"/tmp/script.lobster"}
//   <- {"id":"3","ok":true,"error":"","result":""}
//
// "code" takes precedence over "file" if both are given; a script can report a value back via
// the ts.agent_result() builtin, which ends up in the "result" field.

struct AgentJson {
    static void EscapeInto(std::string &out, std::string_view s) {
        static const char *hex = "0123456789abcdef";
        for (unsigned char c : s) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20) {
                        out += "\\u00";
                        out += hex[(c >> 4) & 0xF];
                        out += hex[c & 0xF];
                    } else {
                        out += static_cast<char>(c);
                    }
            }
        }
    }

    // Reads one JSON string starting at s[i] == '"'. Advances i past the closing quote.
    static bool ParseString(const std::string &s, size_t &i, std::string &out) {
        if (i >= s.size() || s[i] != '"') return false;
        i++;
        out.clear();
        while (i < s.size() && s[i] != '"') {
            char c = s[i];
            if (c != '\\') {
                out += c;
                i++;
                continue;
            }
            i++;
            if (i >= s.size()) return false;
            switch (s[i]) {
                case '"': out += '"'; i++; break;
                case '\\': out += '\\'; i++; break;
                case '/': out += '/'; i++; break;
                case 'n': out += '\n'; i++; break;
                case 'r': out += '\r'; i++; break;
                case 't': out += '\t'; i++; break;
                case 'b': out += '\b'; i++; break;
                case 'f': out += '\f'; i++; break;
                case 'u': {
                    // BMP only, no surrogate pair handling: enough for the ASCII-heavy
                    // control-char escapes this protocol actually needs.
                    if (i + 4 >= s.size()) return false;
                    unsigned cp = 0;
                    for (int k = 1; k <= 4; k++) {
                        char h = s[i + k];
                        int v;
                        if (h >= '0' && h <= '9') {
                            v = h - '0';
                        } else if (h >= 'a' && h <= 'f') {
                            v = h - 'a' + 10;
                        } else if (h >= 'A' && h <= 'F') {
                            v = h - 'A' + 10;
                        } else {
                            return false;
                        }
                        cp = (cp << 4) | static_cast<unsigned>(v);
                    }
                    i += 5;
                    if (cp < 0x80) {
                        out += static_cast<char>(cp);
                    } else if (cp < 0x800) {
                        out += static_cast<char>(0xC0 | (cp >> 6));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    } else {
                        out += static_cast<char>(0xE0 | (cp >> 12));
                        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: return false;
            }
        }
        if (i >= s.size()) return false;  // unterminated string
        i++;                              // closing quote
        return true;
    }

    // Parses a flat {"key":"value",...} object: the only shape this protocol needs.
    static bool ParseFlatObject(const std::string &s, std::map<std::string, std::string> &out) {
        size_t i = 0;
        auto skip_ws = [&] {
            while (i < s.size() && isspace(static_cast<unsigned char>(s[i]))) i++;
        };
        skip_ws();
        if (i >= s.size() || s[i] != '{') return false;
        i++;
        skip_ws();
        if (i < s.size() && s[i] == '}') {
            i++;
            return true;
        }
        for (;;) {
            skip_ws();
            std::string key;
            if (!ParseString(s, i, key)) return false;
            skip_ws();
            if (i >= s.size() || s[i] != ':') return false;
            i++;
            skip_ws();
            std::string value;
            if (!ParseString(s, i, value)) return false;
            out[key] = value;
            skip_ws();
            if (i < s.size() && s[i] == ',') {
                i++;
                continue;
            }
            if (i < s.size() && s[i] == '}') {
                i++;
                return true;
            }
            return false;
        }
    }

    static std::string BuildReply(std::string_view id, bool ok, std::string_view error,
                                  std::string_view result) {
        std::string out = "{\"id\":\"";
        EscapeInto(out, id);
        out += "\",\"ok\":";
        out += (ok ? "true" : "false");
        out += ",\"error\":\"";
        EscapeInto(out, error);
        out += "\",\"result\":\"";
        EscapeInto(out, result);
        out += "\"}";
        return out;
    }
};

#ifdef wxHAS_UNIX_DOMAIN_SOCKETS

struct AgentServer : wxEvtHandler {
    unique_ptr<wxSocketServer> listener;
    std::map<wxSocketBase *, std::string> conns;  // socket -> pending input buffer
    std::string token;
    wxString socket_path;
    wxString token_path;

    ~AgentServer() override { Stop(); }

    static wxString SocketPath() {
        return wxString::Format("/tmp/TreeSheets-agent-%s.sock", wxGetUserId());
    }

    static wxString TokenPath() { return SocketPath() + ".token"; }

    static std::string GenerateToken() {
        static const char *hex = "0123456789abcdef";
        std::random_device rd;
        std::uniform_int_distribution<int> dist(0, 15);
        std::string t(32, '0');
        for (auto &c : t) c = hex[dist(rd)];
        return t;
    }

    bool Start() {
        socket_path = SocketPath();
        token_path = TokenPath();
        ::wxRemoveFile(socket_path);  // stale socket left behind by an unclean exit

        token = GenerateToken();
        wxFile tf;
        if (!tf.Create(token_path, true, wxS_IRUSR | wxS_IWUSR) || !tf.Write(wxString(token))) {
            wxLogWarning("Agent server: could not write token file %s", token_path);
            return false;
        }
        tf.Close();

        wxUNIXaddress addr;
        addr.Filename(socket_path);
        listener = make_unique<wxSocketServer>(addr, wxSOCKET_REUSEADDR);
        if (!listener->IsOk()) {
            wxLogWarning("Agent server: could not listen on %s", socket_path);
            listener.reset();
            return false;
        }
        listener->SetEventHandler(*this);
        listener->SetNotify(wxSOCKET_CONNECTION_FLAG);
        listener->Notify(true);
        Bind(wxEVT_SOCKET, &AgentServer::OnSocketEvent, this);
        return true;
    }

    void Stop() {
        for (auto &kv : conns) kv.first->Destroy();
        conns.clear();
        listener.reset();
        if (!socket_path.IsEmpty()) ::wxRemoveFile(socket_path);
        if (!token_path.IsEmpty()) ::wxRemoveFile(token_path);
    }

    void OnSocketEvent(wxSocketEvent &event) {
        auto *sock = event.GetSocket();

        if (sock == listener.get()) {
            auto *client = listener->Accept(false);
            if (client == nullptr) return;
            client->SetEventHandler(*this);
            client->SetNotify(wxSOCKET_INPUT_FLAG | wxSOCKET_LOST_FLAG);
            client->Notify(true);
            conns[client] = {};
            return;
        }

        auto it = conns.find(sock);
        if (it == conns.end()) return;

        if (event.GetSocketEvent() == wxSOCKET_LOST) {
            sock->Destroy();
            conns.erase(it);
            return;
        }

        char buf[4096];
        sock->Read(buf, sizeof(buf));
        auto n = sock->LastCount();
        if (n == 0) return;
        it->second.append(buf, n);

        size_t nl;
        while ((nl = it->second.find('\n')) != std::string::npos) {
            std::string line = it->second.substr(0, nl);
            it->second.erase(0, nl + 1);
            std::string reply = HandleLine(line);
            reply += '\n';
            sock->Write(reply.data(), reply.size());
        }
    }

    std::string HandleLine(const std::string &line) {
        std::map<std::string, std::string> req;
        if (!AgentJson::ParseFlatObject(line, req)) {
            return AgentJson::BuildReply("", false, "malformed request", "");
        }
        std::string id = req.count("id") ? req["id"] : "";
        if (!req.count("token") || req["token"] != token) {
            return AgentJson::BuildReply(id, false, "bad token", "");
        }
        std::string cmd = req.count("cmd") ? req["cmd"] : "";

        if (cmd == "ping") { return AgentJson::BuildReply(id, true, "", "pong"); }

        if (cmd == "eval") {
            if (!sys || sys->frame == nullptr || sys->frame->GetCurrentTab() == nullptr) {
                return AgentJson::BuildReply(id, false, "no document open", "");
            }
            std::string code = req.count("code") ? req["code"] : "";
            std::string file = req.count("file") ? req["file"] : "agent";
            if (code.empty() && !req.count("file")) {
                return AgentJson::BuildReply(id, false, "eval needs 'code' or 'file'", "");
            }
            auto error = tssi.ScriptRun(file.c_str(), code);
            auto result = tssi.TakeAgentResult();
            return AgentJson::BuildReply(id, error.empty(), error, result);
        }

        return AgentJson::BuildReply(id, false, "unknown cmd", "");
    }
};

#endif  // wxHAS_UNIX_DOMAIN_SOCKETS
