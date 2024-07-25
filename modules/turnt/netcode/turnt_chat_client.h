#ifndef TURNT_CHAT_CLIENT_H
#define TURNT_CHAT_CLIENT_H

#include "core/io/stream_peer_tcp.h"
#include "../turnt_globals.h"

#include "../tnt_macros.h"
#ifdef TNT_CLASS_NAME
#undef TNT_CLASS_NAME
#endif // TNT_CLASS_NAME
#define TNT_CLASS_NAME TurntChatClient

static int DEFAULT_TCP_PORT = 25235;

class TurntChatClient : public StreamPeerTCP
{
    GDCLASS(TurntChatClient, StreamPeerTCP);

public:
    TurntChatClient() = default;

    void _process(float delta) { }

// GDScript-accessible
public:
    TNTPROP_GS(String, host_ip);
    TNTPROP_GS(int, tcp_port);
    TNTPROP_GS(String, auth_token);
    TNTPROP_G(bool, is_authenticated);

    bool start_connection_to_host() { return false; };
    void disconnect_from_host() { } 

protected:
    static void _bind_methods()
    {
        ClassDB::bind_method(D_METHOD("start_connection_to_host"), &TurntChatClient::start_connection_to_host);
        ClassDB::bind_method(D_METHOD("disconnect_from_host"), &TurntChatClient::disconnect_from_host);

        TNTADD_GS(STRING, host_ip);
        TNTADD_GS(INT, tcp_port);
        TNTADD_GS(STRING, auth_token);
        TNTADD_G(BOOL, is_authenticated);
    }

};

#endif // TURNT_CHAT_CLIENT_H