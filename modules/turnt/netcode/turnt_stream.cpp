#include "turnt_stream.h"
#include "core/os/os.h"
#include "../tnt_macros.h"



void TurntStreamWriter::declare_size(const uint64_t in_size)
{
    m_buf.resize(in_size);
}



void TurntStreamWriter::s_u8(const uint8_t in_val)
{
    m_buf.put_u8(in_val);
}



void TurntStreamWriter::s_u16(const uint16_t in_val)
{
    m_buf.put_u16(in_val);
}



void TurntStreamWriter::s_u32(const uint32_t in_val)
{
    m_buf.put_u32(in_val);
}



void TurntStreamWriter::s_u64(const uint64_t in_val)
{
    m_buf.put_u64(in_val);
}



void TurntStreamWriter::s_i8(const int8_t in_val)
{
    m_buf.put_8(in_val);
}



void TurntStreamWriter::s_i16(const int16_t in_val)
{
    m_buf.put_16(in_val);
}



void TurntStreamWriter::s_i32(const int32_t in_val)
{
    m_buf.put_32(in_val);
}



void TurntStreamWriter::s_i64(const int64_t in_val)
{
    m_buf.put_64(in_val);
}



bool TurntStreamWriter::s_utf8(CharString* in_str, const uint64_t max_len)
{
    const int len = in_str->length();
    if (len > max_len)
    {
        TNT_LOGERR_ARGS(TurntStreamWriter, "in_str with len %d exceeds max_len of %llu", len, max_len);
        return false;
    }

    if (max_len <= UINT8_MAX)
    {
        m_buf.put_u8(static_cast<uint8_t>(len));
    }
    else if (max_len <= UINT16_MAX)
    {
        m_buf.put_u16(static_cast<uint16_t>(len));
    }
    else if (max_len <= UINT32_MAX)
    {
        m_buf.put_u32(static_cast<uint32_t>(len));
    }
    else
    {
        TNT_LOGERR(TurntStreamWriter, "in_str cannot exceed the max length of a uint32.. what are you even trying to do?");
        return false;
    }

    // Zero-length strings are allowed - we just record the len as 0 but don't put anything after it
    if (len > 0)
    {
        m_buf.put_data((const uint8_t*)in_str->get_data(), len);
    }

    return true;
}
