// Copyright 2017 Wouter van Oortmerssen. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "lobster/stdafx.h"

#include "lobster/natreg.h"

#include "lobster/glinterface.h"
#include "lobster/sdlinterface.h"

#ifdef PLATFORM_STEAMWORKS

#include "steam/steam_api.h"

struct QueuedMessage {
    SteamNetworkingIdentity identity{};
    string message;
};

struct SteamPeer {
    string ident;
    SteamNetworkingIdentity net_identity;
    HSteamNetConnection connection = k_HSteamNetConnection_Invalid;
    bool is_listen_connection = false;
    bool is_connected = false;
    vector<QueuedMessage> queued_messages;
};

struct SteamState {
    HSteamListenSocket listen_socket = k_HSteamListenSocket_Invalid;
    HSteamNetPollGroup poll_group = k_HSteamNetPollGroup_Invalid;
    vector<SteamPeer> peers;
    bool steamoverlayactive = false;
    // Lobby Data
    CSteamID created_lobby;           // The most recently created lobby.
    CSteamID joined_lobby;            // The most recently joined lobby.
    vector<CSteamID> joined_lobbies;  // A list of all joined lobbies.
    int matched_lobbies = -1;         // -1: not requested, or in progress. >= 0: number of matches

    ~SteamState() {
        for (auto &peer: peers) {
            // Don't close connections that were connected via the listen
            // socket, that seems to crash Steam.
            if (peer.connection != k_HSteamNetConnection_Invalid && !peer.is_listen_connection) {
                LOG_INFO("~SteamState(): closing connection to \"", peer.ident, "\"");
                auto ok = SteamNetworkingSockets()->CloseConnection(
                    peer.connection, k_ESteamNetConnectionEnd_App_Generic, nullptr, false);
                if (!ok) {
                    LOG_ERROR("~SteamState(): closing the connection failed");
                }
            }
        }
        SteamAPI_RunCallbacks();
        // Make sure to close connections before closing the listen socket, that
        // seems to crash Steam.
        if (listen_socket != k_HSteamListenSocket_Invalid) {
            LOG_INFO("~SteamState(): closing listen socket");
            auto ok = SteamNetworkingSockets()->CloseListenSocket(listen_socket);
            if (!ok) LOG_ERROR("~SteamState(): closing listen socket failed");
        }
        SteamAPI_RunCallbacks();
        if (poll_group != k_HSteamNetPollGroup_Invalid) {
            LOG_INFO("~SteamState(): destroying poll group");
            auto ok = SteamNetworkingSockets()->DestroyPollGroup(poll_group);
            if (!ok) LOG_ERROR("~SteamState(): destroying poll group failed");
        }
        SteamAPI_RunCallbacks();
    }

    STEAM_CALLBACK(SteamState, OnGameOverlayActivated, GameOverlayActivated_t);
    STEAM_CALLBACK(SteamState, OnScreenshotRequested, ScreenshotRequested_t);
	STEAM_CALLBACK(SteamState, OnNetConnectionStatusChanged, SteamNetConnectionStatusChangedCallback_t);

    // P2P Functions
    string GetIpAddressIdentity() {
        if (!listen_socket) {
            LOG_ERROR("GetIpAddressIdentity(): listen socket is not open");
            return "";
        }
        SteamNetworkingIPAddr ip_address;
        if (!SteamNetworkingSockets()->GetListenSocketAddress(listen_socket, &ip_address)) {
            LOG_ERROR("GetIpAddressIdentity(): GetListenSocketAddress failed");
            return "";
        }
        SteamNetworkingIdentity id;
        id.m_eType = k_ESteamNetworkingIdentityType_IPAddress;
        id.SetIPAddr(ip_address);
        char ident[SteamNetworkingIdentity::k_cchMaxString];
        id.ToString(ident, sizeof(ident));
        return ident;
    }

    auto FindPeer(const SteamNetworkingIdentity &net_identity) {
        return find_if(peers.begin(), peers.end(), [&](const auto &peer) {
            return peer.net_identity == net_identity;
        });
    }

    auto FindPeer(const string_view ident) {
        return find_if(peers.begin(), peers.end(), [&](const auto& peer) {
            return peer.ident == ident;
        });
    }

    auto FindPeer(HSteamNetConnection conn) {
        return find_if(peers.begin(), peers.end(), [&](const auto& peer) {
            return peer.connection == conn;
        });
    }

    auto FindOrCreatePeer(HSteamNetConnection conn) {
        bool found = true;
        auto peer = FindPeer(conn);
        if (peer == peers.end()) {
            peers.emplace_back();
            peer = peers.end() - 1;
            found = false;
        }
        return make_pair(peer, found);
    }

    auto RenamePeer(string_view_nt str_identity, string_view_nt str_new_identity) {
        auto peer = FindPeer(str_identity.sv);
        if (peer == peers.end()) {
            LOG_ERROR("RenamePeer(): no peer named ", str_identity);
            return false;
        }
        auto new_peer = FindPeer(str_new_identity.sv);
        // Don't allow renaming to a name that already exists.
        if (new_peer != peers.end()) {
            LOG_ERROR("RenamePeer(): peer named ", str_new_identity, " already exists.");
            return false;
        }
        peer->ident = str_new_identity.sv;
        return true;
    }

    bool SetGlobalConfigValue(ESteamNetworkingConfigValue eValue, int val) {
        return SteamNetworkingUtils()->SetGlobalConfigValueInt32(eValue, val);
    }

    bool P2PListen() {
        if (listen_socket != k_HSteamListenSocket_Invalid) {
            LOG_ERROR("P2PListen(): listen socket is already open");
            return false;
        }
        auto socket = SteamNetworkingSockets()->CreateListenSocketP2P(1, 0, nullptr);
        listen_socket = socket;
        LOG_INFO("P2PListen(): created listen socket");
        return socket != k_HSteamListenSocket_Invalid;
    }

    bool P2PListenIP(string_view_nt ip_addr) {
        if (listen_socket != k_HSteamListenSocket_Invalid) {
            LOG_ERROR("P2PListenIP(): listen socket is already open");
            return false;
        }
        SteamNetworkingIPAddr ip;
        ip.ParseString(ip_addr.c_str());
        char real_ip[SteamNetworkingIdentity::k_cchMaxString]{};
        ip.ToString(real_ip, sizeof(real_ip), true);
        auto socket = SteamNetworkingSockets()->CreateListenSocketIP(ip, 0, nullptr);
        listen_socket = socket;
        LOG_INFO("P2PListenIP(): created listen socket for IP Address at ", real_ip);
        SteamNetworkingIPAddr address;
        if (!SteamNetworkingSockets()->GetListenSocketAddress(listen_socket, &address)) {
            LOG_ERROR("P2PListenIP(): GetListenSocketAddress failed");
            return false;
        }
        char ipaddr[SteamNetworkingIPAddr::k_cchMaxString]{};
        address.ToString(ipaddr, sizeof(ipaddr), true);
        LOG_INFO("P2PListenIP(): listen socket IP Address is ", ipaddr);
        return socket != k_HSteamListenSocket_Invalid;
    }

    bool P2PConnect(string_view_nt str_identity) {
        SteamNetworkingIdentity identity{};
        identity.ParseString(str_identity.c_str());
        HSteamNetConnection connection = k_HSteamNetConnection_Invalid;
        if (identity.m_eType == k_ESteamNetworkingIdentityType_IPAddress) {
            const SteamNetworkingIPAddr *ip = identity.GetIPAddr();
            if (ip == NULL) {
                LOG_ERROR("P2PConnect(): identity ", str_identity, " has type IP but GetIPAddr() failed?");
                return false;
            }
            char ip_addr[SteamNetworkingIdentity::k_cchMaxString]{};
            ip->ToString(ip_addr, sizeof(ip_addr), true);
            LOG_INFO("P2PConnect(): opened connection for IP address to ", ip_addr);
            connection = SteamNetworkingSockets()->ConnectByIPAddress(*ip, 0, nullptr);
        } else {
            LOG_INFO("P2PConnect(): opening connection to ", str_identity);
            connection = SteamNetworkingSockets()->ConnectP2P(identity, 1, 0, nullptr);
        }
        if (connection == k_HSteamNetConnection_Invalid) {
            LOG_ERROR("P2PConnect(): failed to open connection to ", str_identity);
            return false;
        }
        peers.push_back(SteamPeer { string(str_identity.sv), {}, connection, false, false, {} });
        return true;
    }

    bool P2PConnectIP(string_view_nt ip_addr) {
        SteamNetworkingIPAddr ip;
        ip.ParseString(ip_addr.c_str());
        char real_ip[SteamNetworkingIdentity::k_cchMaxString]{};
        ip.ToString(real_ip, sizeof(real_ip), true);
        LOG_INFO("P2PConnectIP(): opened connection for IP address to ", ip_addr, " (parsed as ", real_ip, ")");
        auto connection = SteamNetworkingSockets()->ConnectByIPAddress(ip, 0, nullptr);
        if (connection == k_HSteamNetConnection_Invalid) {
            LOG_ERROR("P2PConnectIP(): failed to open connection to ", ip_addr);
            return false;
        }
        // Use the SteamNetworkingIdentity format for the peer's identity (e.g. "ip:<ip_addr>")
        SteamNetworkingIdentity id;
        id.m_eType = k_ESteamNetworkingIdentityType_IPAddress;
        id.SetIPAddr(ip);
        char str_id[SteamNetworkingIdentity::k_cchMaxString];
        id.ToString(str_id, sizeof(str_id));
        peers.push_back(SteamPeer { str_id, {}, connection, false, false, {} });
        return true;
    }

    bool P2PCloseConnection(string_view_nt str_identity, bool linger) {
        auto peer = FindPeer(str_identity.sv);
        if (peer == peers.end()) {
            LOG_ERROR("P2PCloseConnection(): no peer named ", str_identity);
            return false;
        }
        if (!peer->is_connected) {
            LOG_ERROR("P2PCloseConnection(): peer named ", str_identity, " is not connected");
            return false;
        }
        auto ok = SteamNetworkingSockets()->CloseConnection(peer->connection, k_ESteamNetConnectionEnd_App_Generic, "", linger);
        if (ok) {
            peers.erase(peer);
        }
        LOG_INFO("P2PCloseConnection(): connection closed");
        return ok;
    }

    bool CloseListen() {
        if (listen_socket == k_HSteamListenSocket_Invalid) {
            LOG_INFO("CloseListen(): listen socket is already closed");
            return true;
        }
        auto result = SteamNetworkingSockets()->CloseListenSocket(listen_socket);
        LOG_INFO("CloseListen(): closed listen socket");
        listen_socket = k_HSteamListenSocket_Invalid;
        return result;
    }

    bool SendMessage(string_view_nt str_identity, string_view buf, bool reliable) {
        auto peer = FindPeer(str_identity.sv);
        if (peer == peers.end()) {
            LOG_ERROR("SendMessage(): no peer named ", str_identity);
            return false;
        }
        if (!peer->is_connected) {
            if (reliable) {
                LOG_INFO("SendMessage(): queueing message to \"", str_identity, "\"");
                peer->queued_messages.push_back(QueuedMessage { peer->net_identity, string(buf) });
                return true;
            }
            LOG_ERROR("SendMessage(): peer named ", str_identity, " is not connected, dropping unreliable message");
            return false;
        }

        const char *data = buf.data();
        uint32_t size = (uint32_t)buf.size();
        int flags = reliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_Unreliable;
        auto result = SteamNetworkingSockets()->SendMessageToConnection(peer->connection, data, size, flags, nullptr);
        if (result != k_EResultOK) {
            LOG_ERROR("SendMessage(): trying to send message to \"", str_identity, "\" of size ", buf.size() ," got result ",  result);
        }
        return result == k_EResultOK;
    }

    bool BroadcastMessage(string_view buf, bool reliable) {
        const char *data = buf.data();
        uint32_t size = (uint32_t)buf.size();
        int flags = reliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_Unreliable;
        bool ok = true;
        // TODO: We should be able to use SendMessages here so we don't need to
        // make multiple copies of the buffer. The message struct itself is
        // ref-counted, but also contains the destination connection, so
        // it can't be shared. We'd need to have the data itself be ref-counted
        // too.
        for (auto &peer: peers) {
            if (!peer.is_connected) {
                char ident[SteamNetworkingIdentity::k_cchMaxString]{};
                peer.net_identity.ToString(ident, sizeof(ident));
                if (reliable) {
                    LOG_INFO("BroadcastMessage(): queueing message to \"", ident, "\"");
                    peer.queued_messages.push_back(QueuedMessage { peer.net_identity, string(buf) });
                    continue;
                }
                LOG_ERROR("BroadcastMessage(): peer named ", ident, " is not connected, dropping unreliable message");
                continue;
            }
            auto result = SteamNetworkingSockets()->SendMessageToConnection(peer.connection, data, size, flags, nullptr);
            ok &= result == k_EResultOK;
            if (result != k_EResultOK) {
                LOG_ERROR("BroadcastMessage(): trying to send message to \"", peer.ident, "\" of size ", buf.size() ," got result ",  result);
            }
        }
        return ok;
    }

    vector<SteamNetworkingMessage_t*> ReceiveMessages() {
        if (poll_group == k_HSteamNetPollGroup_Invalid) return {};

        vector<SteamNetworkingMessage_t*> messages;
        while (true) {
            constexpr int max_messages = 32;
            SteamNetworkingMessage_t *message_buf[max_messages];
            int res = SteamNetworkingSockets()->ReceiveMessagesOnPollGroup(poll_group, message_buf, max_messages);
            // TODO: better handling of invalid connection?
            if (res <= 0) break;
            for (int i = 0; i < res; ++i) {
                messages.push_back(message_buf[i]);
            }
        }
        return messages;
    }

    void UpdateP2P() {
        for (auto &peer : peers) {
            if (!peer.is_connected) continue;
            // Send the queued messages to the peer, now that they're connected.
            char ident[SteamNetworkingIdentity::k_cchMaxString]{};
            peer.net_identity.ToString(ident, sizeof(ident));
            for (auto &message : peer.queued_messages) {
                const char *data = message.message.data();
                uint32_t size = (uint32_t)message.message.size();
                int flags = k_nSteamNetworkingSend_Reliable;
                auto result = SteamNetworkingSockets()->SendMessageToConnection(peer.connection, data, size, flags, nullptr);
                if (result != k_EResultOK) {
                    LOG_ERROR("UpdateP2P(): trying to send message to \"", ident, "\" of size ", size ," got result ",  result);
                }
            }
            peer.queued_messages.clear();
        }
    }

    bool GetConnectionRealTimeStatus(string_view_nt str_identity, SteamNetConnectionRealTimeStatus_t* status) {
        auto peer = FindPeer(str_identity.sv);
        if (peer == peers.end()) {
            LOG_ERROR("GetConnectionRealTimeStatus(): no peer named ", str_identity);
            return false;
        }
        if (!peer->is_connected) {
            LOG_ERROR("GetConnectionRealTimeStatus(): peer named ", str_identity, " is not connected");
            return false;
        }
        auto result = SteamNetworkingSockets()->GetConnectionRealTimeStatus(peer->connection, status, 0, nullptr);
        if (result != k_EResultOK) {
            LOG_ERROR("GetConnectionRealTimeStatus(): failed to get realtime status of ", str_identity, " result=", result);
            return false;
        }
        return true;
    }

    // Lobby Functions
    STEAM_CALLBACK(SteamState, OnLobbyDataUpdate, LobbyDataUpdate_t);

    bool CreateLobby(int lobby_type, int max_members) {
        if (lobby_type < 0 || lobby_type > k_ELobbyTypeInvisible) {
            return false;
        }
        if (OnLobbyCreatedCallback.IsActive()) {
            return false;
        }
        created_lobby.Clear();
        joined_lobby.Clear();
        auto result = SteamMatchmaking()->CreateLobby((ELobbyType)lobby_type, max_members);
        OnLobbyCreatedCallback.Set(result, this, &SteamState::OnLobbyCreated);
        return true;
    }
	CCallResult<SteamState, LobbyCreated_t> OnLobbyCreatedCallback;
    void OnLobbyCreated(LobbyCreated_t *pCallback, bool bIOFailure);

    bool JoinLobby(CSteamID steam_id) {
        if (OnLobbyEnteredCallback.IsActive()) {
            return false;
        }
        joined_lobby.Clear();
        auto result = SteamMatchmaking()->JoinLobby(steam_id);
        OnLobbyEnteredCallback.Set(result, this, &SteamState::OnLobbyEntered);
        return true;
    }
	CCallResult<SteamState, LobbyEnter_t> OnLobbyEnteredCallback;
    void OnLobbyEntered(LobbyEnter_t *pCallback, bool bIOFailure);

    bool LeaveLobby(CSteamID steam_id) {
        SteamMatchmaking()->LeaveLobby(steam_id);
        auto iter = find(joined_lobbies.begin(), joined_lobbies.end(), steam_id);
        if (iter != joined_lobbies.end()) {
            joined_lobbies.erase(iter);
        }
        return true;
    }

    bool SetLobbyJoinable(CSteamID steam_id, bool joinable) {
        return SteamMatchmaking()->SetLobbyJoinable(steam_id, joinable);
    }

    const char* GetLobbyData(CSteamID steam_id, const char* key) {
        auto result = SteamMatchmaking()->GetLobbyData(steam_id, key);
        return result;
    }

    CSteamID GetLobbyOwner(CSteamID steam_id) {
        auto result = SteamMatchmaking()->GetLobbyOwner(steam_id);
        return result;
    }

    int GetLobbyDataCount(CSteamID steam_id) {
        auto count = SteamMatchmaking()->GetLobbyDataCount(steam_id);
        return count;
    }

    int GetLobbyDataByIndex(CSteamID steam_id, int index, char *key, int key_size, char *value, int value_size) {
        auto count = SteamMatchmaking()->GetLobbyDataByIndex(steam_id, index, key, key_size, value, value_size);
        return count;
    }

    bool SetLobbyData(CSteamID steam_id, const char* key, const char* value) {
        auto ok = SteamMatchmaking()->SetLobbyData(steam_id, key, value);
        return ok;
    }

    bool DeleteLobbyData(CSteamID steam_id, const char* key) {
        auto ok = SteamMatchmaking()->DeleteLobbyData(steam_id, key);
        return ok;
    }

    int GetNumLobbyMembers(CSteamID steam_id) {
        return SteamMatchmaking()->GetNumLobbyMembers(steam_id);
    }

    CSteamID GetLobbyMemberByIndex(CSteamID steam_id, int index) {
        return SteamMatchmaking()->GetLobbyMemberByIndex(steam_id, index);
    }

    bool AddRequestLobbyListNumericalFilter(const char *key, int value, ELobbyComparison cmp) {
        SteamMatchmaking()->AddRequestLobbyListNumericalFilter(key, value, cmp);
        return true;
    }

    bool AddRequestLobbyListStringFilter(const char *key, const char *value, ELobbyComparison cmp) {
        SteamMatchmaking()->AddRequestLobbyListStringFilter(key, value, cmp);
        return true;
    }

    bool AddRequestLobbyListResultCountFilter(int count) {
        SteamMatchmaking()->AddRequestLobbyListResultCountFilter(count);
        return true;
    }

    bool RequestLobbyData(CSteamID steam_id) {
        auto ok = SteamMatchmaking()->RequestLobbyData(steam_id);
        return ok;
    }

    bool LobbyListIsReady() {
        return matched_lobbies >= 0;
    }

    bool LobbyListReset() {
        matched_lobbies = -1;
        return true;
    }

    bool RequestLobbyList() {
        if (OnLobbyMatchListCallback.IsActive()) {
            return false;
        }
        auto result = SteamMatchmaking()->RequestLobbyList();
        OnLobbyMatchListCallback.Set(result, this, &SteamState::OnLobbyMatchList);
        matched_lobbies = -1;
        return true;
    }
    CCallResult<SteamState, LobbyMatchList_t> OnLobbyMatchListCallback;
    void OnLobbyMatchList(LobbyMatchList_t *pLobbyMatchList, bool bIOFailure);

    CSteamID GetLobbyByIndex(int index) {
        return SteamMatchmaking()->GetLobbyByIndex(index);
    }

    CSteamID GetLobbyGameServer(CSteamID lobby_id) {
        CSteamID server_id;
        SteamMatchmaking()->GetLobbyGameServer(lobby_id, nullptr, nullptr, &server_id);
        return server_id;
    }

    bool SetLobbyGameServer(CSteamID lobby_id, CSteamID server_id) {
        SteamMatchmaking()->SetLobbyGameServer(lobby_id, 0, 0, server_id);
        return true;
    }

    const char *GetFriendPersonaName(CSteamID steam_id) {
        return SteamFriends()->GetFriendPersonaName(steam_id);
    }

    bool HasFriend(CSteamID steam_id, int friend_flags) {
        return SteamFriends()->HasFriend(steam_id, friend_flags);
    }
};

void SteamState::OnGameOverlayActivated(GameOverlayActivated_t *callback) {
    steamoverlayactive = callback->m_bActive;
    LOG_INFO("steam overlay toggle: ", steamoverlayactive);
}

void SteamState::OnScreenshotRequested(ScreenshotRequested_t *) {
    LOG_INFO("steam screenshot requested");
    auto size = GetScreenSize();
    auto pixels = ReadPixels(int2(0), size);
    SteamScreenshots()->WriteScreenshot(pixels, size.x * size.y * 3, size.x, size.y);
    delete[] pixels;
}

void SteamState::OnNetConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t *callback) {
	HSteamNetConnection conn = callback->m_hConn;
	SteamNetConnectionInfo_t info = callback->m_info;
	ESteamNetworkingConnectionState old_state = callback->m_eOldState;
	ESteamNetworkingConnectionState new_state = info.m_eState;

    SteamNetworkingIdentity remote_identity = info.m_identityRemote;
    char ident[SteamNetworkingIdentity::k_cchMaxString]{};
    remote_identity.ToString(ident, sizeof(ident));
    char ipaddr[SteamNetworkingIPAddr::k_cchMaxString]{};
    info.m_addrRemote.ToString(ipaddr, sizeof(ipaddr), true);

    // If the local identity and the remote identity are the same and they're
    // both using Steam ID, then we're trying to connect two clients on the same
    // machine. So just use the IP address as the identity instead.
    SteamNetworkingIdentity local_identity{};
    SteamNetworkingSockets()->GetIdentity(&local_identity);
    if (local_identity == remote_identity && local_identity.m_eType == k_ESteamNetworkingIdentityType_SteamID) {
        SteamNetworkingIdentity ip_identity;
        ip_identity.m_eType = k_ESteamNetworkingIdentityType_IPAddress;
        ip_identity.SetIPAddr(info.m_addrRemote);
        ip_identity.ToString(ident, sizeof(ident));
        remote_identity = ip_identity;
    }

    auto get_state_name = [](auto state) -> string {
        if (state >= 0 && state <= 5) {
            static const char* state_names[] = {
                "none",      "connecting",     "finding route",
                "connected", "closed by peer", "problem detected locally",
            };
            return state_names[state];
        }
        return to_string(state);
    };
    LOG_INFO("OnNetConnectionStatusChanged(): steam net connection status changed: ident=", ident, " ipaddr=", ipaddr, " old state='",
             get_state_name(old_state), "' new state='", get_state_name(new_state), "'");

	if (info.m_hListenSocket != k_HSteamListenSocket_Invalid &&
		old_state == k_ESteamNetworkingConnectionState_None &&
		new_state == k_ESteamNetworkingConnectionState_Connecting) {
        // For now at least, only allow one connection to a peer with a given steam ID.
        auto peer_by_ident = FindPeer(remote_identity);
        if (peer_by_ident != peers.end()) {
            LOG_ERROR("OnNetConnectionStatusChanged(): Peer \"", ident, "\" connecting, but already connected?");
            SteamNetworkingSockets()->CloseConnection(conn, k_ESteamNetConnectionEnd_AppException_Generic, "Already connected", false );
            peers.erase(peer_by_ident);
            return;
        }
        // Find or create the peer with this connection. It may have already
        // been created if we connected with P2PConnect*.
        auto [ peer, found_peer ] = FindOrCreatePeer(conn);
        EResult res = SteamNetworkingSockets()->AcceptConnection(conn);
        if (res != k_EResultOK) {
            LOG_ERROR("OnNetConnectionStatusChanged(): AcceptConnection failed with error=", res);
            SteamNetworkingSockets()->CloseConnection(conn, k_ESteamNetConnectionEnd_AppException_Generic, "Failed to accept connection", false );
            peers.erase(peer);
            return;
        }
        peer->connection = conn;
        peer->ident = ident;
        peer->net_identity = remote_identity;
        peer->is_connected = false;
        peer->is_listen_connection = peer->is_listen_connection || !found_peer;
        LOG_INFO("OnNetConnectionStatusChanged(): OnNetConnectionStatusChanged(): Connecting peer \"", ident, "\"");
        // TODO: start authentication here, if needed
    } else if (new_state == k_ESteamNetworkingConnectionState_Connected) {
        auto [ peer, found_peer ] = FindOrCreatePeer(conn);
        peer->connection = conn;
        peer->ident = ident;
        peer->net_identity = remote_identity;
        peer->is_connected = true;
        peer->is_listen_connection = peer->is_listen_connection || !found_peer;
        if (poll_group == k_HSteamNetPollGroup_Invalid) {
            LOG_INFO("OnNetConnectionStatusChanged(): Creating poll group");
            poll_group = SteamNetworkingSockets()->CreatePollGroup();
        }
        SteamNetworkingSockets()->SetConnectionPollGroup(conn, poll_group);
        LOG_INFO("OnNetConnectionStatusChanged(): Connected peer \"", ident, "\"");
	} else if (new_state == k_ESteamNetworkingConnectionState_ClosedByPeer ||
               new_state == k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
		// Handle disconnecting a peer
        auto peer = FindPeer(conn);
        if (peer != peers.end()) {
            auto ok = SteamNetworkingSockets()->CloseConnection(
                peer->connection, k_ESteamNetConnectionEnd_App_Generic, nullptr, false);
            peers.erase(peer);
            LOG_INFO("OnNetConnectionStatusChanged(): Disconnected from peer \"", ident, "\" msg=\"", info.m_szEndDebug, "\" close connection ok?=", ok);
        } else {
            LOG_INFO("OnNetConnectionStatusChanged(): Peer \"", ident, "\" disconnecting, but not in list?  msg=\"", info.m_szEndDebug, "\"");
        }
	}
}

// Lobby callbacks
void SteamState::OnLobbyCreated(LobbyCreated_t *callback, bool /*io_failure*/) {
    LOG_INFO("OnLobbyCreated(): result=", callback->m_eResult, " lobby=", callback->m_ulSteamIDLobby);
    if (callback->m_eResult == k_EResultOK) {
        CSteamID lobby_id(callback->m_ulSteamIDLobby);
        created_lobby = lobby_id;
        joined_lobby = lobby_id;
        auto iter = find(joined_lobbies.begin(), joined_lobbies.end(), lobby_id);
        if (iter == joined_lobbies.end()) {
            joined_lobbies.push_back(lobby_id);
        } else {
            LOG_INFO("OnLobbyCreated(): Already joined lobby=", callback->m_ulSteamIDLobby, "?");
        }
    }
}

void SteamState::OnLobbyEntered(LobbyEnter_t *callback, bool /*io_failure*/) {
    LOG_INFO("OnLobbyEntered(): lobby=", callback->m_ulSteamIDLobby,
             " permissions=", callback->m_rgfChatPermissions, " locked=", callback->m_bLocked, " response=", callback->m_EChatRoomEnterResponse);
    if (callback->m_EChatRoomEnterResponse == k_EChatRoomEnterResponseSuccess) {
        CSteamID lobby_id(callback->m_ulSteamIDLobby);
        joined_lobby = lobby_id;
        auto iter = find(joined_lobbies.begin(), joined_lobbies.end(), lobby_id);
        if (iter == joined_lobbies.end()) {
            joined_lobbies.push_back(lobby_id);
        } else {
            LOG_INFO("OnLobbyEntered(): Already joined lobby=", callback->m_ulSteamIDLobby, "?");
        }
    }
}

void SteamState::OnLobbyMatchList(LobbyMatchList_t *lobby_match_list, bool /*io_failure*/) {
    matched_lobbies = lobby_match_list->m_nLobbiesMatching;
    LOG_INFO("OnLobbyMatchList(): Matched ", matched_lobbies, " lobbies.");
}

void SteamState::OnLobbyDataUpdate(LobbyDataUpdate_t* callback) {
    LOG_INFO("OnLobbyDataUpdate(): lobby=", callback->m_ulSteamIDLobby,
             " member=", callback->m_ulSteamIDMember, " success=", callback->m_bSuccess);
}

extern "C" void __cdecl SteamAPIDebugTextHook(int severity, const char *debugtext) {
    if (severity) LOG_WARN(debugtext) else LOG_INFO(debugtext);
}

SteamState *steam = nullptr;

#endif  // PLATFORM_STEAMWORKS

void SteamShutDown() {
    #ifdef PLATFORM_STEAMWORKS
        if (steam) {
            delete steam;
            SteamAPI_Shutdown();
        }
        steam = nullptr;
    #endif  // PLATFORM_STEAMWORKS
}

int SteamInit(iint appid, bool screenshots, bool initrelay) {
    SteamShutDown();
    #ifdef PLATFORM_STEAMWORKS
        #ifndef NDEBUG
            (void)appid;
        #else
            if (appid && SteamAPI_RestartAppIfNecessary((uint32_t)appid)) {
                LOG_INFO("Not started from Steam");
                return -1;
            }
        #endif
        bool steaminit = SteamAPI_Init();
        LOG_INFO("Steam init: ", steaminit);
        if (!steaminit) return 0;
        SteamUtils()->SetWarningMessageHook(&SteamAPIDebugTextHook);
        steam = new SteamState();
        SteamUserStats()->RequestCurrentStats();
        if (screenshots) SteamScreenshots()->HookScreenshots(true);
        if (initrelay) SteamNetworkingUtils()->InitRelayNetworkAccess();
        return 1;
    #else
        return 0;
    #endif  // PLATFORM_STEAMWORKS
}

void SteamUpdate() {
    #ifdef PLATFORM_STEAMWORKS
        if (steam) {
            SteamAPI_RunCallbacks();
            steam->UpdateP2P();
        }
    #endif  // PLATFORM_STEAMWORKS
}

const char *UserName() {
    #ifdef PLATFORM_STEAMWORKS
        if (steam) return SteamFriends()->GetPersonaName();
    #endif  // PLATFORM_STEAMWORKS
    return "";
}

bool UnlockAchievement(string_view_nt name) {
    #ifdef PLATFORM_STEAMWORKS
        if (steam) {
            auto ok = SteamUserStats()->SetAchievement(name.c_str());
            return SteamUserStats()->StoreStats() && ok;  // Force this to run.
        }
    #else
        (void)name;
    #endif  // PLATFORM_STEAMWORKS
    return false;
}

int SteamReadFile(string_view_nt fn, string &buf) {
    #ifdef PLATFORM_STEAMWORKS
        if (steam) {
            auto len = SteamRemoteStorage()->GetFileSize(fn.c_str());
            if (len) {
                buf.resize(len);
                return SteamRemoteStorage()->FileRead(fn.c_str(), (void *)buf.data(), len);
            }
        }
    #endif  // PLATFORM_STEAMWORKS
    return 0;
}

bool SteamWriteFile(string_view_nt fn, string_view buf) {
    #ifdef PLATFORM_STEAMWORKS
        if (steam) {
        return SteamRemoteStorage()->FileWrite(fn.c_str(), buf.data(), (int)buf.size());
        }
    #endif  // PLATFORM_STEAMWORKS
    return false;
}

bool OverlayActive() {
    #ifdef PLATFORM_STEAMWORKS
        return steam && steam->steamoverlayactive;
    #endif  // PLATFORM_STEAMWORKS
    return false;
}

using namespace lobster;

#ifdef PLATFORM_STEAMWORKS
LString* GetIdentityString(VM &vm, const SteamNetworkingIdentity &identity) {
    char ident[SteamNetworkingIdentity::k_cchMaxString];
    identity.ToString(ident, sizeof(ident));
    return vm.NewString(ident);
}

CSteamID SteamIDFromValue(Value &steam_id) {
    return CSteamID((uint64)steam_id.ival());
}

iint IIntFromSteamID(const CSteamID &steam_id) {
    return (iint)steam_id.ConvertToUint64();
}
#endif

#ifdef PLATFORM_STEAMWORKS
#define STEAM_BOOL_VALUE(...)  Value(steam ? (__VA_ARGS__) : false)
#define STEAM_INT_VALUE(...)  Value(steam ? (__VA_ARGS__) : 0)
#define STEAM_IINT_VALUE(...)  Value(steam ? (__VA_ARGS__) : (iint)0)
#define STEAM_STRING_VALUE(vm, ...)  Value(vm.NewString(steam ? (__VA_ARGS__) : ""))
#else
#define STEAM_BOOL_VALUE(...) Value(false)
#define STEAM_INT_VALUE(...)  Value(0)
#define STEAM_IINT_VALUE(...)  Value((iint)0)
#define STEAM_STRING_VALUE(vm, ...)  Value(vm.NewString(""));
#endif

void AddSteam(NativeRegistry &nfr) {

nfr("init", "appid,allowscreenshots,initrelay", "IBB", "I",
    "initializes SteamWorks. returns 1 if succesful, 0 on failure. Specify a non-0 appid if you"
    " want to restart from steam if this wasn't started from steam (the return value in this"
    " case will be -1 to indicate you should terminate this instance). If you don't specify an"
    " appid here or in steam_appid.txt, init will likely fail. The other functions can still be"
    " called even if steam isn't active."
    " allowscreenshots automatically uploads screenshots to steam (triggered by steam)."
    " initrelay initializes the relay network for p2p early, to save time when it is first used.",
    [](StackPtr &, VM &, Value &appid, Value &ss, Value &relay) {
        return Value(SteamInit(appid.ival(), ss.True(), relay.True()));
    });

nfr("shutdown", "", "", "",
    "", [](StackPtr &, VM &) { SteamShutDown(); });

nfr("overlay", "", "", "B",
    "returns true if the steam overlay is currently on (you may want to auto-pause if so)",
    [](StackPtr &, VM &) {
        return Value(OverlayActive());
    });

nfr("username", "", "", "S",
    "returns the name of the steam user, or empty string if not available.",
    [](StackPtr &, VM &vm) {
        return Value(vm.NewString(UserName()));
    });

nfr("unlock_achievement", "achievementname", "S", "B",
    "Unlocks an achievement and shows the achievement overlay if not already achieved before."
    " Will also Q-up saving achievement to Steam."
    " Returns true if succesful.",
    [](StackPtr &, VM &, Value &name) {
        auto ok = UnlockAchievement(name.sval()->strvnt());
        return Value(ok);
    });

nfr("write_file", "file,contents", "SS", "B",
    "writes a file with the contents of a string to the steam cloud, or local storage if that"
    " fails, returns false if writing wasn't possible at all",
    [](StackPtr &, VM &, Value &file, Value &contents) {
        auto fn = file.sval()->strvnt();
        auto s = contents.sval();
        auto ok = SteamWriteFile(fn, s->strv());
        if (!ok) {
            ok = WriteFile(fn.sv, true, s->strv(), false);
        }
        return Value(ok);
    });

nfr("read_file", "file", "S", "S?",
    "returns the contents of a file as a string from the steam cloud if available, or otherwise"
    " from local storage, or nil if the file can't be found at all.",
    [](StackPtr &, VM &vm, Value &file) {
        auto fn = file.sval()->strvnt();
        string buf;
        auto len = SteamReadFile(fn, buf);
        if (!len) len = (int)LoadFile(fn.sv, &buf);
        if (len < 0) return NilVal();
        auto s = vm.NewString(buf);
        return Value(s);
    });

nfr("update", "", "", "",
    "you must call this function in your game loop when using most steam APIs",
    [](StackPtr &, VM &) {
        SteamUpdate();
    });

nfr("get_steam_id", "", "", "I", "get the steam id of the current user",
    [](StackPtr &, VM &) {
        return STEAM_IINT_VALUE(IIntFromSteamID(SteamUser()->GetSteamID()));
    });

nfr("friend_get_username", "steam_id", "I", "S",
    "returns the name for the given steam id; this only works for friends, or users in the same "
    "lobby, chat room, game server, etc.",
    [](StackPtr &, VM &vm, Value &steam_id) {
        return STEAM_STRING_VALUE(vm, steam->GetFriendPersonaName(SteamIDFromValue(steam_id)));
    });

nfr("has_friend", "steam_id,flags", "II", "B",
    "returns true if the given steam_id is a friend with the matching flags",
    [](StackPtr &, VM &vm, Value &steam_id, Value &friend_flags) {
        return STEAM_BOOL_VALUE(vm, steam->HasFriend(SteamIDFromValue(steam_id), friend_flags.intval()));
    });

nfr("net_identity", "", "", "S",
    "returns the steam identity for this"
    " user. This same ID will be used for connecting to peers, sending messages,"
    " etc.",
    [](StackPtr &, VM &vm) {
        #ifdef PLATFORM_STEAMWORKS
            if (steam) {
                SteamNetworkingIdentity identity{};
                SteamNetworkingSockets()->GetIdentity(&identity);
                return Value(GetIdentityString(vm, identity));
            }
        #endif
        return Value(vm.NewString("none"));
    });

nfr("net_identity_from_steam_id", "steam_id", "I", "S",
    "returns a network identity for the given steam id", [](StackPtr &, VM &vm, Value &steam_id) {
        #ifdef PLATFORM_STEAMWORKS
            if (steam) {
                SteamNetworkingIdentity identity{};
                identity.SetSteamID((uint64)steam_id.ival());
                return Value(GetIdentityString(vm, identity));
            }
        #endif
        return Value(vm.NewString("none"));
    });

nfr("net_identity_ipaddr", "", "", "S",
    "returns the steam identity for this user, but as an IP address instead of a"
    " Steam ID. This can be useful when connecting two clients that are using the"
    " same Steam ID (e.g. on the same computer). NOTE: this will only work if "
    " p2p_listen_ipaddr is called first.",
    [](StackPtr &, VM &vm) {
        return STEAM_STRING_VALUE(vm, steam->GetIpAddressIdentity());
    });

nfr("p2p_set_send_buffer_size", "size", "I", "B", "set the upper limit of pending bytes to be sent",
    [](StackPtr &, VM &, Value &size) {
        auto ok = STEAM_BOOL_VALUE(steam->SetGlobalConfigValue(k_ESteamNetworkingConfig_SendBufferSize, size.intval()));
        return Value(ok);
    });

nfr("p2p_set_recv_buffer_size", "size", "I", "",
    "upper limit on total size in bytes of received messages that will be buffered waiting "
    "to be processed by the application",
    [](StackPtr &, VM &, Value &size) {
        auto ok = STEAM_BOOL_VALUE(steam->SetGlobalConfigValue(k_ESteamNetworkingConfig_RecvBufferSize, size.intval()));
        return Value(ok);
    });

nfr("p2p_get_connection_status", "ident", "S", "IFFFFFFIIIII",
    "receive realtime connection status info. Returned values are: ping, local quality, "
    "remote quality, out packets/sec, out bytes/sec, in packets/sec, in bytes/sec, send rate bytes/sec, "
    "pending unreliable packets, pending reliable packets, sent unACKed reliable packets, and queue time in usec. "
    "See ISteamNetworkingSockets::GetConnectionRealTimeStatus() for more info.",
    [](StackPtr &sp, VM &) {
        #ifdef PLATFORM_STEAMWORKS
            if (steam) {
                auto ident = Pop(sp).sval()->strvnt();
                SteamNetConnectionRealTimeStatus_t status;
                steam->GetConnectionRealTimeStatus(ident, &status);
                Push(sp, Value(status.m_nPing));
                Push(sp, Value(status.m_flConnectionQualityLocal));
                Push(sp, Value(status.m_flConnectionQualityRemote));
                Push(sp, Value(status.m_flOutPacketsPerSec));
                Push(sp, Value(status.m_flOutBytesPerSec));
                Push(sp, Value(status.m_flInPacketsPerSec));
                Push(sp, Value(status.m_flInBytesPerSec));
                Push(sp, Value(status.m_nSendRateBytesPerSecond));
                Push(sp, Value(status.m_cbPendingUnreliable));
                Push(sp, Value(status.m_cbPendingReliable));
                Push(sp, Value(status.m_cbSentUnackedReliable));
                Push(sp, Value(status.m_usecQueueTime));
                return;
            }
        #endif
        Push(sp, Value(0));
        Push(sp, Value(0.0f));
        Push(sp, Value(0.0f));
        Push(sp, Value(0.0f));
        Push(sp, Value(0.0f));
        Push(sp, Value(0.0f));
        Push(sp, Value(0.0f));
        Push(sp, Value(0));
        Push(sp, Value(0));
        Push(sp, Value(0));
        Push(sp, Value(0));
        Push(sp, Value(0));
    });

nfr("p2p_listen", "", "", "B", "open a listen socket to receive new connections",
    [](StackPtr &, VM &) {
        return STEAM_BOOL_VALUE(steam->P2PListen());
    });

nfr("p2p_listen_ipaddr", "ipaddr", "S", "B", "open a listen socket for receive new IP address connections",
    [](StackPtr &, VM &, Value &ipaddr) {
        return STEAM_BOOL_VALUE(steam->P2PListenIP(ipaddr.sval()->strvnt()));
    });

nfr("p2p_close_listen", "", "", "B", "close the listen socket and stop accepting new connections",
    [](StackPtr &, VM &) {
        return STEAM_BOOL_VALUE(steam->CloseListen());
    });

nfr("p2p_connect", "ident", "S", "B", "connect to a user with a given steam "
    "identity (e.g. \"steam:XXXX\") or ip address (e.g. \"ip:127.0.0.1:5105\") that "
    "has opened a listen socket",
    [](StackPtr &, VM &, Value &ident) {
        return STEAM_BOOL_VALUE(steam->P2PConnect(ident.sval()->strvnt()));
    });

nfr("p2p_connect_ipaddr", "ipaddr", "S", "B", "connect to a user with a given IP address that has opened a listen socket",
    [](StackPtr &, VM &, Value &ipaddr) {
        return STEAM_BOOL_VALUE(steam->P2PConnectIP(ipaddr.sval()->strvnt()));
    });

nfr("p2p_close_connection", "ident,linger", "SB", "B",
    "close a connection opened with p2p_connect(); if linger is true then the connection will "
    "remain open for a short time to finish pending messages",
    [](StackPtr &, VM &, Value &ident, Value &linger) {
        return STEAM_BOOL_VALUE(steam->P2PCloseConnection(ident.sval()->strvnt(), linger.intval()));
    });

nfr("p2p_get_connections", "", "", "S]", "get a list of the steam identites that are currently connected",
    [](StackPtr &, VM &vm) {
        auto *peers_vec = vm.NewVec(0, 0, TYPE_ELEM_VECTOR_OF_STRING);

        #ifdef PLATFORM_STEAMWORKS
            if (steam) {
                SteamNetworkingIdentity local_identity{};
                SteamNetworkingSockets()->GetIdentity(&local_identity);
                for (auto &peer: steam->peers) {
                    if (!peer.is_connected) continue;
                    peers_vec->Push(vm, vm.NewString(peer.ident));
                }
            }
        #endif  // PLATFORM_STEAMWORKS

        return Value(peers_vec);
    });

nfr("p2p_rename_peer", "ident,new_ident", "SS", "B", "use a different identifier"
    " for this peer. This can be useful when connecting multiple users using"
    " peer-to-peer, so you can have all peers in the network use the listen socket IP"
    " address and port as the identifier.",
    [](StackPtr &, VM &, Value &ident, Value &new_ident) {
        return STEAM_BOOL_VALUE(steam->RenamePeer(ident.sval()->strvnt(), new_ident.sval()->strvnt()));
    });

nfr("p2p_send_message", "ident,data,reliable", "SSB", "B", "send a reliable message to a given steam identity",
    [](StackPtr &, VM &, Value &ident, Value &data, Value &reliable) {
        return STEAM_BOOL_VALUE(steam->SendMessage(ident.sval()->strvnt(), data.sval()->strv(), reliable.intval()));
    });

nfr("p2p_broadcast_message", "data,reliable", "SB", "B", "send a reliable message to all connected peers",
    [](StackPtr &, VM &, Value &data, Value &reliable) {
        return STEAM_BOOL_VALUE(steam->BroadcastMessage(data.sval()->strv(), reliable.intval()));
    });

nfr("p2p_receive_messages", "", "", "S]S]", "receive messages from all"
    " connected peers. The first return value is an array of messages, the second"
    " return value is an array of the steam identities that sent each message",
    [](StackPtr &sp, VM &vm) {
        auto *client_vec = vm.NewVec(0, 0, TYPE_ELEM_VECTOR_OF_STRING);
        auto *data_vec = vm.NewVec(0, 0, TYPE_ELEM_VECTOR_OF_STRING);

        #ifdef PLATFORM_STEAMWORKS
            if (steam) {
                auto messages = steam->ReceiveMessages();
                for (auto *message: messages) {
                    auto *data = vm.NewString(string_view((char*)message->m_pData, message->m_cbSize));
                    auto peer = steam->FindPeer(message->m_conn);  // TODO: optimize?
                    if (peer != steam->peers.end()) {
                        client_vec->Push(vm, vm.NewString(peer->ident));
                        data_vec->Push(vm, data);
                    }
                    message->Release();
                }
            }
        #endif  // PLATFORM_STEAMWORKS

        Push(sp, data_vec);
        Push(sp, client_vec);
    });

nfr("lobby_create", "max_members", "I", "B",
    "create a new lobby that allows at most a given number of members; this lobby will be "
    "automatically joined. use lobby_get_created() to get the newly created lobby's steam id",
    [](StackPtr &, VM &, Value &max_members) {
        return STEAM_BOOL_VALUE(steam->CreateLobby(k_ELobbyTypePublic, max_members.intval()));
    });

nfr("lobby_create", "lobby_type,max_members", "II", "B",
    "create a new lobby of a given lobby type that allows at most a given number "
    "of members; this lobby will be automatically joined. use lobby_get_created() "
    "to get the newly created lobby's steam id",
    [](StackPtr &, VM &, Value &lobby_type, Value &max_members) {
        return STEAM_BOOL_VALUE(steam->CreateLobby(lobby_type.intval(), max_members.intval()));
    });

nfr("lobby_get_created", "", "", "I",
    "get the steam id of the most recently created lobby",
    [](StackPtr &, VM &) {
        return STEAM_IINT_VALUE(IIntFromSteamID(steam->created_lobby));
    });

nfr("lobby_join", "steam_id", "I", "B", "join a lobby with the given steam id",
    [](StackPtr &, VM &, Value &steam_id) {
        return STEAM_BOOL_VALUE(steam->JoinLobby(SteamIDFromValue(steam_id)));
    });

nfr("lobby_leave", "steam_id", "I", "B", "leave a lobby with the given steam id",
    [](StackPtr &, VM &, Value &steam_id) {
        return STEAM_BOOL_VALUE(steam->LeaveLobby(SteamIDFromValue(steam_id)));
    });

nfr("lobby_set_joinable", "steam_id,joinable", "IB", "B",
    "mark a lobby as joinable; only works if you are the owner",
    [](StackPtr &, VM &, Value &steam_id, Value &joinable) {
        return STEAM_BOOL_VALUE(
            steam->SetLobbyJoinable(SteamIDFromValue(steam_id), joinable.intval()));
    });

nfr("lobby_get_joined", "", "", "I",
    "get a list of the most recent lobby joined with lobby_create() or lobby_join()",
    [](StackPtr &, VM &) {
        return STEAM_IINT_VALUE(IIntFromSteamID(steam->joined_lobby));
    });

nfr("lobby_get_all_joined", "", "", "I]",
    "get a list of all of the lobbies that have been joined with lobby_create() or lobby_join()",
    [](StackPtr &, VM &vm) {
        auto *lobbies_vec = vm.NewVec(0, 0, TYPE_ELEM_VECTOR_OF_INT);

        #ifdef PLATFORM_STEAMWORKS
            if (steam) {
                for (const auto &lobby_id : steam->joined_lobbies) {
                    lobbies_vec->Push(vm, (iint)lobby_id.ConvertToUint64());
                }
            }
        #endif  // PLATFORM_STEAMWORKS

        return Value(lobbies_vec);
    });

nfr("lobby_get_owner", "steam_id", "I", "I",
    "get the steam id of the owner of the given lobby",
    [](StackPtr &, VM &, Value &steam_id) {
        return STEAM_IINT_VALUE(IIntFromSteamID(steam->GetLobbyOwner(SteamIDFromValue(steam_id))));
    });

nfr("lobby_request_data", "steam_id", "I", "B",
    "refresh data for a given lobby; it is not necessary to call this for any lobby that you have "
    "joined",
    [](StackPtr &, VM &, Value &steam_id) {
        return STEAM_BOOL_VALUE(steam->RequestLobbyData(SteamIDFromValue(steam_id)));
    });

nfr("lobby_get_data", "steam_id,key", "IS", "S",
    "get the matching value for a given key stored on this lobby; if the key has not been set then "
    "the result is an empty string",
    [](StackPtr &, VM &vm, Value &steam_id, Value &key) {
        return STEAM_STRING_VALUE(
            vm, steam->GetLobbyData(SteamIDFromValue(steam_id), key.sval()->strvnt().c_str()));
    });

nfr("lobby_get_all_data", "steam_id", "I", "S]S]", "get all key-value pairs stored on this lobby",
    [](StackPtr &sp, VM &vm) {
        auto vsteam_id = Pop(sp);
        (void)vsteam_id;
        auto *key_vec = vm.NewVec(0, 0, TYPE_ELEM_VECTOR_OF_STRING);
        auto *value_vec = vm.NewVec(0, 0, TYPE_ELEM_VECTOR_OF_STRING);

        #ifdef PLATFORM_STEAMWORKS
            if (steam) {
                CSteamID steam_id = SteamIDFromValue(vsteam_id);
                auto count = steam->GetLobbyDataCount(steam_id);
                for (int i = 0; i < count; ++i) {
                    char key[k_nMaxLobbyKeyLength];
                    char value[k_cubChatMetadataMax];
                    auto ok =
                        steam->GetLobbyDataByIndex(steam_id, i, key, sizeof(key), value, sizeof(value));
                    if (ok) {
                        key_vec->Push(vm, vm.NewString(key));
                        value_vec->Push(vm, vm.NewString(value));
                    }
                }
            }
        #endif  // PLATFORM_STEAMWORKS

        Push(sp, key_vec);
        Push(sp, value_vec);
    });

nfr("lobby_set_data", "steam_id,key,value", "ISS", "B",
    "set a key-value pair for this lobby; only works if you are the owner",
    [](StackPtr &, VM &, Value &steam_id, Value &key, Value &value) {
        return STEAM_BOOL_VALUE(steam->SetLobbyData(SteamIDFromValue(steam_id),
                                                    key.sval()->strvnt().c_str(),
                                                    value.sval()->strvnt().c_str()));
    });

nfr("lobby_delete_data", "steam_id,key", "IS", "B",
    "delete a key-value pair for this lobby; only works if you are the owner",
    [](StackPtr &, VM &, Value &steam_id, Value &key) {
        return STEAM_BOOL_VALUE(
            steam->DeleteLobbyData(SteamIDFromValue(steam_id), key.sval()->strvnt().c_str()));
    });

nfr("lobby_get_num_members", "steam_id", "I", "I", "get the number of members in this lobby",
    [](StackPtr &, VM &, Value &steam_id) {
        return STEAM_INT_VALUE(steam->GetNumLobbyMembers(SteamIDFromValue(steam_id)));
    });

nfr("lobby_get_members", "steam_id", "I", "I]",
    "get the steam ids of all members in this lobby; only works if you have joined the lobby",
    [](StackPtr &, VM &vm, Value &vsteam_id) {
        auto *members_vec = vm.NewVec(0, 0, TYPE_ELEM_VECTOR_OF_INT);

        #ifdef PLATFORM_STEAMWORKS
            if (steam) {
                CSteamID steam_id = SteamIDFromValue(vsteam_id);
                int num_members = steam->GetNumLobbyMembers(steam_id);
                for (int i = 0; i < num_members; ++i) {
                    auto id = steam->GetLobbyMemberByIndex(steam_id, i);
                    if (id.IsValid()) {
                        // NOTE: Can't use id.Render() because it seems to be unimplemented?
                        members_vec->Push(vm, (iint)id.ConvertToUint64());
                    }
                }
            }
        #endif  // PLATFORM_STEAMWORKS

        return Value(members_vec);
    });

nfr("lobby_request_add_numerical_filter", "key,value,cmp", "SII", "B",
    "add a numerical filter for the next lobby request",
    [](StackPtr &, VM &, Value &key, Value &value, Value &cmp) {
        return STEAM_BOOL_VALUE(steam->AddRequestLobbyListNumericalFilter(
            key.sval()->strvnt().c_str(), value.intval(), (ELobbyComparison)cmp.intval()));
    });

nfr("lobby_request_add_string_filter", "key,value,cmp", "SSI", "B",
    "add a string filter for the next lobby request",
    [](StackPtr &, VM &, Value &key, Value &value, Value &cmp) {
        return STEAM_BOOL_VALUE(steam->AddRequestLobbyListStringFilter(
            key.sval()->strvnt().c_str(), value.sval()->strvnt().c_str(),
            (ELobbyComparison)cmp.intval()));
    });

nfr("lobby_request_add_result_count_filter", "count", "I", "B",
    "add a result count limit for the next lobby request", [](StackPtr &, VM &, Value &count) {
        return STEAM_BOOL_VALUE(steam->AddRequestLobbyListResultCountFilter(count.intval()));
    });

nfr("lobby_request_list", "", "", "B",
    "request a list of lobbies that match the current set of filters; this function completes "
    "asynchronously, call lobby_request_is_ready() to determine when it is ready and "
    "lobby_request_get_lobbies() to get the results",
    [](StackPtr &, VM &) {
        return STEAM_BOOL_VALUE(steam->RequestLobbyList());
    });

nfr("lobby_request_list_reset", "", "", "",
    "clear the list of matched lobbies, so lobby_request_is_ready() returns false",
    [](StackPtr &, VM &) {
        return STEAM_BOOL_VALUE(steam->LobbyListReset());
    });

nfr("lobby_request_is_ready", "", "", "B",
    "returns true when a call to lobby_request_list() has finished",
    [](StackPtr &, VM &) {
        return STEAM_BOOL_VALUE(steam->LobbyListIsReady());
    });

nfr("lobby_request_get_lobbies", "", "", "I]",
    "returns the list of matched lobbies when lobby_request_list() has finished",
    [](StackPtr &, VM &vm) {
        auto *lobbies_vec = vm.NewVec(0, 0, TYPE_ELEM_VECTOR_OF_INT);

        #ifdef PLATFORM_STEAMWORKS
            if (steam && steam->matched_lobbies >= 0) {
                for (int i = 0; i < steam->matched_lobbies; ++i) {
                    auto id = steam->GetLobbyByIndex(i);
                    if (id.IsValid()) {
                        // NOTE: Can't use id.Render() because it seems to be unimplemented?
                        lobbies_vec->Push(vm, (iint)id.ConvertToUint64());
                    }
                }
            }
        #endif  // PLATFORM_STEAMWORKS

        return Value(lobbies_vec);
    });

nfr("lobby_get_game_server", "lobby_id", "I", "I", "get the game server associated with this lobby",
    [](StackPtr &, VM &, Value &lobby_id) {
        return STEAM_IINT_VALUE(
            IIntFromSteamID(steam->GetLobbyGameServer(SteamIDFromValue(lobby_id))));
    });

nfr("lobby_set_game_server", "lobby_id,server_id", "II", "B",
    "set the game server associated with this lobby; only works if you are the owner",
    [](StackPtr &, VM &, Value &lobby_id, Value &server_id) {
        return STEAM_BOOL_VALUE(
            steam->SetLobbyGameServer(SteamIDFromValue(lobby_id), SteamIDFromValue(server_id)));
    });

}  // AddSteam