#ifndef TURNT_CHAT_PACKETS_H
#define TURNT_CHAT_PACKETS_H

#include <cstdint>
#include "core/ustring.h"

// Note: Packet structs are prefixed with TP for "TurntPacket"
//       Yes yes I know I'm very imaginative 

// Auth tokens are variable length and could be fuckin anything
// For now I'm enforcing a max size of 512kb for the sake of preventing easy DDOS
constexpr uint32_t MAX_AUTHTOKEN_LEN = 512 * 1024;



struct TPClientAuthToken
{
    CharString auth_token;

    template<typename NetStream> bool Serialise(NetStream& ns)
    {
        if (!ns.s_utf8(auth_token.get_data(), auth_token.length(), MAX_AUTHTOKEN_LEN))
        {
            return false;
        }

        return true;
    }
};



#endif // TURNT_CHAT_PACKETS_H