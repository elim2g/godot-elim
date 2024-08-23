#ifndef TURNT_CHAT_PACKETS_H
#define TURNT_CHAT_PACKETS_H

#include <cstdint>
#include "core/ustring.h"
#include "turnt_net_def.h"

// Note: Packet structs are prefixed with TP for "TurntPacket"
//       Yes yes I know I'm very imaginative 

// AuthTokens can be variable size. IDK what max value to enforce so we're stickin with 512kb for now.
// Anything larger than that can simply get fucked for now
constexpr uint64_t MAX_AUTHTOKEN_LEN = 1024 * 512;

struct TPClientAuthToken
{
    CharString auth_token;

    template<typename NetStream> bool Serialise(NetStream& ns)
    {
        ns.declare_size(sizeof(net_id_t) + sizeof(authtoken_len_t) + MAX_AUTHTOKEN_LEN);

        ns.s_u8(NID_AUTHTOKEN)

        if (!ns.s_utf8(auth_token, MAX_AUTHTOKEN_LEN))
        {
            return false;
        }

        return true;
    }
};



#endif // TURNT_CHAT_PACKETS_H