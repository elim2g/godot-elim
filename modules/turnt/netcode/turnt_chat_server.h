#ifndef TURNT_CHAT_SERVER
#define TURNT_CHAT_SERVER

#include "core/io/tcp_server.h"
#include "core/os/os.h"
#include "turnt_globals.h"

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

public:
    TNTPROP_GS(int, num_max_connections);
    TNTPROP_GS(int, tcp_port);

private:
    void handle_new_connection() { }

protected:
    static void _bind_methods()
    {
        ClassDB::bind_method(D_METHOD("start_server"), &TurntChatServer::start_server);    
        ClassDB::bind_method(D_METHOD("stop_server"), &TurntChatServer::start_server);    

        TNTADD_GS(INT, num_max_connections);
        TNTADD_GS(INT, tcp_port);
    }
};

#endif // TURNT_CHAT_SERVER