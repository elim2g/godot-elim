#ifndef TURNT_CHAT_PEER_H
#define TURNT_CHAT_PEER_H

#include "core/io/stream_peer_tcp.h"
#include "core/os/os.h"

class TurntChatPeer
{
public:
    TurntChatPeer(Ref<StreamPeerTCP> peer, uint16_t local_id)
        : m_peer(peer)
        , m_local_id(local_id)
        , m_authenticated(false)
        , m_is_authenticating(false)
        , m_tick_auth_started(0)
        , m_tick_last_heartbeat(0)
    {
        //
    }

    Ref<StreamPeerTCP> get_peer() { return m_peer; }
    uint16_t get_local_id() const { return m_local_id; }
    bool is_authenticated() const { return m_authenticated; }
    bool is_authenticating() const { return m_is_authenticating; }
    uint32_t get_tick_auth_started() const { return m_tick_auth_started; }
    uint32_t get_tick_last_heartbeat() const { return m_tick_last_heartbeat; }
    // TODO
    void terminate_connection() { }

private:
    Ref<StreamPeerTCP> m_peer;
    uint16_t m_local_id;
    bool m_authenticated;
    bool m_is_authenticating;
    uint32_t m_tick_auth_started;
    uint32_t m_tick_last_heartbeat;
};

#endif // TURNT_CHAT_PEER_H