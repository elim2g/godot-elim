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



class TurntStreamReader
{
public:
    TurntStreamReader(Ref<StreamPeerBuffer> in_buf)
        : m_buf(in_buf) {}

    void declare_size(const uint64_t in_size) {} // Only needed to match TurntStreamWriter definition

    void s_u8(uint8_t& out_val);
    void s_u16(uint16_t& out_val);
    void s_u32(uint32_t& out_val);
    void s_u64(uint64_t& out_val);

    void s_i8(int8_t& out_val);
    void s_i16(int16_t& out_val);
    void s_i32(int32_t& out_val);
    void s_i64(int64_t& out_val);

    bool s_utf8(CharString* out_str, const uint64_t max_len);

private:
    Ref<StreamPeerBuffer> m_buf;

private:
    // Will cause assert if trying to read past buffer
    inline void ensure_data_avail(const uint64_t num_bytes);
};

#endif // TURNT_STREAM_H