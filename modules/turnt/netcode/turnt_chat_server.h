#ifndef TURNT_CHAT_SERVER_H
#define TURNT_CHAT_SERVER_H

#include "core/io/tcp_server.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "turnt_globals.h"

#include "turnt_chat_peer.h"
#include "tnt_macros.h"
#ifdef TNT_CLASS_NAME
#undef TNT_CLASS_NAME
#endif // TNT_CLASS_NAME
#define TNT_CLASS_NAME TurntChatServer

// TODO: Replace with C++ version of TntLogger once ready
#define TNT_LOG(msg, ...) OS::get_singleton()->print("[ChatServer] " msg "\n", __VA_ARGS__)
#define TNT_LOGERR(msg, ...) OS::get_singleton()->printerr("[ChatServer] " msg "\n", __VA_ARGS__)

static int DEFAULT_NUM_MAX_CONNECTIONS = 64;
static int DEFAULT_PORT = 25235;

class TurntChatServer : public TCP_Server
{
    GDCLASS(TurntChatServer, TCP_Server);

public:
    TurntChatServer()
        : m_num_max_connections(DEFAULT_NUM_MAX_CONNECTIONS)
        , m_tcp_port(DEFAULT_PORT)
    { }

    bool start_server() 
    { 
        if (is_listening())
        {
            TNT_LOG("start_server() called when already listening!");
            return true; // Return true since if we're already listening, then we technically HAVE successfully started the server..
        }

        Error err = listen(static_cast<uint16_t>(m_tcp_port));
        if (err != Error::OK)
        {
            TNT_LOGERR("Error listening on port %d with err code %d", m_tcp_port, err);
            return false;
        }
        else
        {
            TNT_LOG("Started listening on port %d", m_tcp_port);
            return true;
        }
    }

    void stop_server() 
    { 
        if (!is_listening())
        {
            TNT_LOG("stop_server() called when not listening!");
            return;
        }

        stop();
        TNT_LOG("Stopped listening. Connections will now be closed.");
        // TODO: Loop through connections and kick em awf, kid
    }

    void _process(float delta)
    {
        // Check if peers awaiting authentication have authed or timed out
        for (uint32_t i = m_peers_awaiting_auth.size()-1; i >= 0; --i)
        {
            TurntChatPeer* tcp = m_peers_awaiting_auth[i];
            // TODO: Check for timeout
            if (tcp->is_authenticated())
            {
                m_authenticated_peers.append(tcp);
                m_peers_awaiting_auth.remove(i);
                continue;
            }
            // If something prevented authentication, it's cy@ l8r
            else if (!tcp->is_authenticating())
            {
                // TODO: Safe fragging (removal from m_peers_awaiting auth and m_chat_peers)
            }
        }

        // TODO: Loop through authenticated peers
        //       Check if they need to be deleted (ie disconnect)
        //       Otherwise check for chat messages and do chat things

        // Handle any new incoming connections
        handle_incoming_connections();
    }

public:
    TNTPROP_GS(int, num_max_connections);
    TNTPROP_GS(int, tcp_port);

private:
    void handle_incoming_connections() 
    { 
        // We enforce a max time to spend inside of the "while" loop to prevent softlocks
        const uint64_t tick_started = Time::get_singleton()->get_ticks_msec();
        constexpr uint64_t timeout_msec = 1000;
        const uint64_t timeout_tick = tick_started + timeout_msec;

        // Keep accepting new connections until we run out of them or hit our timeout limit
        while ((is_connection_available()) && (Time::get_singleton()->get_ticks_msec() < timeout_tick))
        {
            TurntChatPeer* new_peer = memnew(TurntChatPeer(take_connection()));
            m_chat_peers.append(new_peer);
            m_peers_awaiting_auth.append(new_peer);
            // TODO: Kick off auth process
        }
    }

private:
    PoolVector<TurntChatPeer*> m_chat_peers;
    PoolVector<TurntChatPeer*> m_peers_awaiting_auth;
    PoolVector<TurntChatPeer*> m_authenticated_peers;

protected:
    static void _bind_methods()
    {
        ClassDB::bind_method(D_METHOD("start_server"), &TurntChatServer::start_server);    
        ClassDB::bind_method(D_METHOD("stop_server"), &TurntChatServer::start_server);    

        TNTADD_GS(INT, num_max_connections);
        TNTADD_GS(INT, tcp_port);
    }
};

#endif // TURNT_CHAT_SERVER_H