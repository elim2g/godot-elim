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

};

#endif // TURNT_CHAT_SERVER