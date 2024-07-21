#ifndef TURNT_CHAT_SERVER
#define TURNT_CHAT_SERVER

#include "core/io/tcp_server.h"
#include "turnt_globals.h"

#include "tnt_macros.h"
#ifdef TNT_CLASS_NAME
#undef TNT_CLASS_NAME
#endif // TNT_CLASS_NAME
#define TNT_CLASS_NAME TurntChatServer

class TurntChatServer : public TCP_Server
{
    GDCLASS(TurntChatServer, TCP_Server);

public:
    TurntChatServer() = default;

    bool start_server() { return false; }
    void stop_server() { }

    TNTPROP_GS(int, num_max_connections);
    TNTPROP_G(bool, is_online);

private:
    void handle_new_connection() { }

protected:
    static void _bind_methods()
    {
        ClassDB::bind_method(D_METHOD("start_server"), &TurntChatServer::start_server);    
        ClassDB::bind_method(D_METHOD("stop_server"), &TurntChatServer::start_server);    

        TNTADD_GS(INT, num_max_connections);
        TNTADD_G(BOOL, is_online);
    }
};

#endif // TURNT_CHAT_SERVER