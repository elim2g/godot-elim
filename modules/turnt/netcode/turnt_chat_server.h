#ifndef TURNT_CHAT_SERVER_H
#define TURNT_CHAT_SERVER_H

#include "turnt_chat_peer.h"
#include "../turnt_globals.h"
#include "../tnt_macros.h"

static int DEFAULT_NUM_MAX_CONNECTIONS = 64;
static int DEFAULT_PORT = 25235;

class TCP_Server;

class TurntChatServer : public TCP_Server
{
    GDCLASS(TurntChatServer, TCP_Server);

public:
    TurntChatServer();

    void _process(float delta);

// GDScript-accessible
public:
    TNTPROP_GS(int, num_max_connections);
    TNTPROP_GS(int, tcp_port);

    bool start_server();
    void stop_server();

private:
    // Wrap back around to 0
    // CLEANUP: I mean technically if low-id clients stay connected for a long time, we'll hit collisions lma0
    inline uint16_t get_next_local_id() { return (m_next_local_id + 1) & ((1 << 16)-1); }

    void handle_incoming_connections();
    void terminate_and_remove_peer(const uint16_t plid);

private:
    uint16_t m_next_local_id;
    PoolVector<TurntChatPeer*> m_chat_peers;
    PoolVector<TurntChatPeer*> m_peers_awaiting_auth;
    PoolVector<TurntChatPeer*> m_authenticated_peers;

protected:
    static void _bind_methods();
};

#endif // TURNT_CHAT_SERVER_H