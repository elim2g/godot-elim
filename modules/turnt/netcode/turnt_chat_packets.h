#ifndef TURNT_CHAT_PACKETS_H
#define TURNT_CHAT_PACKETS_H

#include <cstdint>
#include "core/ustring.h"
#include "turnt_net_def.h"

// Note: Packet structs are prefixed with TP for "TurntPacket"
//       Yes yes I know I'm very imaginative 

struct TPClientAuthToken
{
    CharString auth_token;

    template<typename NetStream> bool Serialise(NetStream& ns)
    {
        ns.declare_size(AUTHTOKEN_HEADER_BYTES + MAX_AUTHTOKEN_LEN);
        if (!ns.s_utf8<MAX_AUTHTOKEN_LEN>(auth_token, MAX_AUTHTOKEN_LEN))
        {
            return false;
        }

        return true;
    }
};



#endif // TURNT_CHAT_PACKETS_H