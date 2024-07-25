#include "turnt_chat_server.h"



TurntChatServer::TurntChatServer()
    : m_next_local_id(0u)
    , m_num_max_connections(DEFAULT_NUM_MAX_CONNECTIONS)
    , m_tcp_port(DEFAULT_PORT)
{
    //
}



void TurntChatServer::_process(float delta)
{
    // Cycle through the poor sods in the w8ing room (peers pending auth)
    // We iterate backwards here because terminating a client will remove them from the vector immediately
    for (uint32_t i = m_peers_awaiting_auth.size()-1; i >= 0; --i)
    {
        TurntChatPeer* tcp = m_peers_awaiting_auth[i];

        // Transient network failures, timeouts n shit
        if (tcp->is_peer_unhealthy())
        {
            terminate_and_remove_peer(tcp->get_local_id());
            continue;
        }

        // Once the client has authenticated, we allow them to participate in The Chats(TM)
        if (tcp->is_authenticated())
        {
            m_authenticated_peers.push_back(tcp);
            m_peers_awaiting_auth.remove(i);

            continue;
        }
        // If something prevented authentication, it's cy@ l8r
        else if (!tcp->is_authenticating())
        {
            terminate_and_remove_peer(tcp->get_local_id());
            continue;
        }
    }

    // TODO: Loop through authenticated peers
    //       Check if they need to be deleted (ie disconnect)
    //       Otherwise check for chat messages and do chat things

    // Handle any new incoming connections
    handle_incoming_connections();
}



bool TurntChatServer::start_server()
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



void TurntChatServer::stop_server()
{ 
    if (!is_listening())
    {
        TNT_LOG("stop_server() called when not listening!");
        return;
    }

    stop();
    TNT_LOG("Stopped listening. Connections will now be closed.");

    if (m_chat_peers.size() > 0)
    {
        PoolVector<uint16_t> active_plids;
        active_plids.resize(m_chat_peers.size());

        for (uint32_t i = 0; i < m_chat_peers.size(); ++i)
        {
            active_plids.push_back(m_chat_peers[i]->get_local_id());
        }

        for (uint32_t i = 0; i < active_plids.size(); ++i)
        {
            terminate_and_remove_peer(active_plids[i]);
        }

        TNT_LOG("All peers terminated.");
    }
}



void TurntChatServer::handle_incoming_connections()
{ 
    // We enforce a max time to spend inside of the "while" loop to prevent softlocks
    const uint64_t tick_started = Time::get_singleton()->get_ticks_msec();
    constexpr uint64_t timeout_msec = 1000;
    const uint64_t timeout_tick = tick_started + timeout_msec;

    // Keep accepting new connections until we run out of them or hit our timeout limit
    while ((is_connection_available()) && (Time::get_singleton()->get_ticks_msec() < timeout_tick))
    {
        TurntChatPeer* new_peer = memnew(TurntChatPeer(take_connection(), get_next_local_id()));
        m_chat_peers.push_back(new_peer);
        m_peers_awaiting_auth.push_back(new_peer);
        TNT_LOG("New peer accepted. Handed local_id %d", new_peer->get_local_id());
        // TODO: Kick off auth process
    }
}



void TurntChatServer::terminate_and_remove_peer(const uint16_t plid)
{
    TurntChatPeer* tcp = nullptr;

    // m_chat_peers
    for (uint32_t i = m_chat_peers.size()-1; i >= 0; --i)
    {
        TurntChatPeer* p = m_chat_peers[i];
        if (p->get_local_id() == plid)
        {
            tcp = p;
            tcp->terminate_connection();
            m_chat_peers.remove(i);

            break;
        }
    }

    if (tcp == nullptr)
    {
        TNT_LOGERR("terminate_and_remove_peer unable to find peer with plid %d", plid);
    }

    // m_peers_awaiting_auth
    for (uint32_t i = m_peers_awaiting_auth.size()-1; i >= 0; --i)
    {
        TurntChatPeer* p = m_peers_awaiting_auth[i];
        if (p->get_local_id() == plid)
        {
            m_peers_awaiting_auth.remove(i);

            // Defensive programming
            if (tcp == nullptr)
            {
                tcp = p;
                tcp->terminate_connection();
                TNT_LOGERR("terminate_and_remove_peer found missing peer %d in m_peers_awaiting_auth", plid);
            }

            break;
        }
    }

    // m_authenticated_peers
    for (uint32_t i = m_authenticated_peers.size()-1; i >= 0; --i)
    {
        TurntChatPeer* p = m_authenticated_peers[i];
        if (p->get_local_id() == plid)
        {
            m_authenticated_peers.remove(i);

            // Defensive programming
            if (tcp == nullptr)
            {
                tcp = p;
                tcp->terminate_connection();
                TNT_LOGERR("terminate_and_remove_peer found missing peer %d in m_authenticated_peers", plid);
            }

            break;
        }
    }

    if (tcp)
    {
        memdelete(tcp);
        TNT_LOG("Terminated peer %d", plid);
    }
}



/*static*/ void TurntChatServer::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("start_server"), &TurntChatServer::start_server);    
    ClassDB::bind_method(D_METHOD("stop_server"), &TurntChatServer::stop_server);    

    TNTADD_GS(INT, num_max_connections);
    TNTADD_GS(INT, tcp_port);
}