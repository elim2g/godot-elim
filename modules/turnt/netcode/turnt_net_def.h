#ifndef TURNT_NET_DEF_H
#define TURNT_NET_DEF_H

#include <cstdint>

constexpr uint64_t MAX_UTF8_STR_LEN_BYTES = 512; // UTF8, 512 characters
constexpr uint64_t MAX_CHAT_STR_LEN_BYTES = 1024; // UTF16, 512 characters

// Auth tokens are variable length and could be fuckin anything
// For now I'm enforcing a max size of 512kb for the sake of preventing easy DDOS
constexpr uint64_t MAX_AUTHTOKEN_LEN = 512 * 1024;

constexpr uint64_t AUTHTOKEN_HEADER_BYTES = sizeof(uint32_t);

#endif // TURNT_NET_DEF_H