#ifndef TURNT_STREAM_H
#define TURNT_STREAM_H

#include <limits.h>
#include "core/io/stream_peer.h"
#include "turnt_net_def.h"

class TurntStreamWriter
{
public:
    TurntStreamWriter() = default;

    void declare_size(const uint64_t in_size)
    {
        m_buf.resize(in_size);
    }

    template<uint64_t T_MAX_LEN>
    bool s_utf8(CharString* in_str, const uint64_t max_len)
    {
        const int len = in_str->length();
        if (len > max_len)
        {
            return false;
        }

        // Write the string length
        // Any self-respecting compiler will eliminate branches here as we're passing
        //  a compile-time constant to check against
        if (max_len < UINT8_MAX)
        {
            m_buf.put_u8(static_cast<uint8_t>(len));
        }
        else if (max_len < UINT16_MAX)
        {
            m_buf.put_u16(static_cast<uint16_t>(len));
        }
        else if (max_len < UINT32_MAX)
        {
            m_buf.put_u32(static_cast<uint32_t>(len));
        }
        else
        {
            // Bro if I'm writing strings that require a 64-byte length prefix I shouldn't
            //  be writing software aye
            m_buf.put_u64(static_cast<uint64_t>(len));
        }

        return (len > 0)    ? (m_buf.put_data(in_str->get_data(), len) == Error::OK)
                            : true; // Zero-length strings are allowed. We just don't write any data after the length.
    }

private:
    StreamPeerBuffer m_buf;
};

#endif // TURNT_STREAM_H