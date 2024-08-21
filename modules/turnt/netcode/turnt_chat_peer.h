#ifndef TURNT_CHAT_PEER_H
#define TURNT_CHAT_PEER_H

#include "core/io/tcp_server.h"
#include "core/os/os.h"
#include "core/os/time.h"

constexpr uint64_t HEARTBEAT_TIMEOUT_MS = 60 * 1000; // 60 seconds
constexpr uint64_t AUTH_TIMEOUT_MS = 30 * 1000; // 30 seconds

class TurntChatPeer
{
public:
    TurntChatPeer(Ref<StreamPeerTCP> peer, uint16_t local_id)
        : m_peer(peer)
        , m_local_id(local_id)
        , m_authenticated(false)
        , m_is_authenticating(false)
        , m_tick_auth_started(0u)
        , m_tick_last_heartbeat(0u)
    {
        //
    }

    Ref<StreamPeerTCP> get_peer() { return m_peer; }
    uint16_t get_local_id() const { return m_local_id; }
    bool is_authenticated() const { return m_authenticated; }
    bool is_authenticating() const { return m_is_authenticating; }
    uint64_t get_tick_auth_started() const { return m_tick_auth_started; }
    uint64_t get_tick_last_heartbeat() const { return m_tick_last_heartbeat; }

    bool is_peer_unhealthy()
    {
        // If the socket is in an error state, we've already got what we need
        const StreamPeerTCP::Status status = m_peer->get_status();
        if (status != StreamPeerTCP::Status::STATUS_CONNECTING && status != StreamPeerTCP::Status::STATUS_CONNECTED)
        {
            return true;
        }

        // If the socket is healthy, we're now checking our own timeout conditions
        if (m_is_authenticating)
        {
            const uint64_t auth_timeout_tick = m_tick_auth_started + AUTH_TIMEOUT_MS;
            if (Time::get_singleton()->get_ticks_msec() >= auth_timeout_tick)
            {
                return true;
            }
        }
        else
        {
            const uint32_t timeout_tick = m_tick_last_heartbeat + HEARTBEAT_TIMEOUT_MS;
            if (Time::get_singleton()->get_ticks_msec() >= timeout_tick)
            {
                return true;
            }
        }

        return false;
    }

    bool is_peer_healthy() { return !is_peer_unhealthy(); }

    // Leaving force as false will allow any remaining data to finish sending before closing the socket
    void terminate_connection(const bool force = false) 
    { 
        if (force)
        {
            m_peer->disconnect_from_host();
            return;
        }

        // TODO: Send incomplete data
        //       Possibly enforce a max limit. If limit of unsent data is exceeded, we frag it anyway

        m_peer->disconnect_from_host();
        return;
    }

private:
    Ref<StreamPeerTCP> m_peer;
    uint16_t m_local_id;
    bool m_authenticated;
    bool m_is_authenticating;
    uint64_t m_tick_auth_started;
    uint64_t m_tick_last_heartbeat;
};

#endif // TURNT_CHAT_PEER_H