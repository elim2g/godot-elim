#ifndef TURNT_STREAM_H
#define TURNT_STREAM_H

#include <limits.h>
#include "core/io/stream_peer.h"
#include "turnt_net_def.h"

class TurntStreamWriter
{
public:
    TurntStreamWriter() = default;

    void declare_size(const uint64_t in_size);

    void s_u8(const uint8_t in_val);
    void s_u16(const uint16_t in_val);
    void s_u32(const uint32_t in_val);
    void s_u64(const uint64_t in_val);
    void s_i8(const int8_t in_val);
    void s_i16(const int16_t in_val);
    void s_i32(const int32_t in_val);
    void s_i64(const int64_t in_val);

    // max_len - Safeguard. If in_str's length exceeds this, the serialisation will fail
    bool s_utf8(CharString* in_str, const uint64_t max_len);

private:
    StreamPeerBuffer m_buf;
};

#endif // TURNT_STREAM_H