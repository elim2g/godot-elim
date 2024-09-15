#include "turnt_stream.h"
#include "core/os/os.h"
#include "../tnt_macros.h"



// TURNT STREAM WRITER
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
// !TURNT STREAM WRITER



// TURNT STREAM READER
void TurntStreamReader::s_u8(uint8_t& out_val)
{
    ensure_data_avail(sizeof(out_val));
    out_val = m_buf->get_u8();
}



void TurntStreamReader::s_u16(uint16_t& out_val)
{
    ensure_data_avail(sizeof(out_val));
    out_val = m_buf->get_u16();
}



void TurntStreamReader::s_u32(uint32_t& out_val)
{
    ensure_data_avail(sizeof(out_val));
    out_val = m_buf->get_u32();
}



void TurntStreamReader::s_u64(uint64_t& out_val)
{
    ensure_data_avail(sizeof(out_val));
    out_val = m_buf->get_u64();
}



void TurntStreamReader::s_i8(int8_t& out_val)
{
    ensure_data_avail(sizeof(out_val));
    out_val = m_buf->get_8();
}



void TurntStreamReader::s_i16(int16_t& out_val)
{
    ensure_data_avail(sizeof(out_val));
    out_val = m_buf->get_16();
}



void TurntStreamReader::s_i32(int32_t& out_val)
{
    ensure_data_avail(sizeof(out_val));
    out_val = m_buf->get_32();
}



void TurntStreamReader::s_i64(int64_t& out_val)
{
    ensure_data_avail(sizeof(out_val));
    out_val = m_buf->get_64();
}



bool TurntStreamReader::s_utf8(CharString* out_str, const uint64_t max_len)
{
    uint64_t strlen = 0;
    if (max_len <= UINT8_MAX)
    {
        ensure_data_avail(sizeof(uint8_t));
        strlen = static_cast<uint64_t>(m_buf->get_u8());
    }
    else if (max_len <= UINT16_MAX)
    {
        ensure_data_avail(sizeof(uint16_t));
        strlen = static_cast<uint64_t>(m_buf->get_u16());
    }
    else if (max_len <= UINT32_MAX)
    {
        ensure_data_avail(sizeof(uint32_t));
        strlen = static_cast<uint64_t>(m_buf->get_u32());
    }
    else
    {
        TNT_LOGERR_ARGS(TurntStreamReader, "Cannot read a string with length potentially longer than what can be held in a u32: max_len %llu", max_len);
        return false;
    }

    // Zero-length strings are allowed
    if (strlen <= 0)
    {
        *out_str = "";
        return true;
    }

    ensure_data_avail(strlen);
    m_buf->get_data((uint8_t*)out_str->ptr(), strlen);

    return true;
}



void TurntStreamReader::ensure_data_avail(const uint64_t num_bytes)
{
    const bool safe_to_read = (m_buf->get_size() - m_buf->get_position()) > num_bytes;
    ERR_FAIL_COND_MSG(!safe_to_read, "[TurntStreamReader] Attempting to read more data from buffer than available");
}
// !TURNT STREAM READER