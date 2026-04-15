#pragma once

#include <string>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <stdint.h>

namespace doriax::editor{
    class SHA1{

    public:
        inline static uint32_t rol(const uint32_t value, const size_t bits) {
            return (value << bits) | (value >> (32 - bits));
        }

        inline static std::string hash(const std::string& input) {
            uint32_t h0 = 0x67452301;
            uint32_t h1 = 0xEFCDAB89;
            uint32_t h2 = 0x98BADCFE;
            uint32_t h3 = 0x10325476;
            uint32_t h4 = 0xC3D2E1F0;

            std::string msg = input;
            size_t original_len = msg.size() * 8;

            // Pre-processing
            msg += static_cast<char>(0x80);
            while ((msg.size() * 8) % 512 != 448) {
                msg += static_cast<char>(0x00);
            }

            for (int i = 7; i >= 0; --i) {
                msg += static_cast<char>((original_len >> (i * 8)) & 0xFF);
            }

            // Process message in 512-bit chunks
            for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
                uint32_t w[80];
                for (int i = 0; i < 16; ++i) {
                    w[i] = (static_cast<uint8_t>(msg[chunk + i * 4 + 0]) << 24) |
                        (static_cast<uint8_t>(msg[chunk + i * 4 + 1]) << 16) |
                        (static_cast<uint8_t>(msg[chunk + i * 4 + 2]) << 8) |
                        (static_cast<uint8_t>(msg[chunk + i * 4 + 3]));
                }
                for (int i = 16; i < 80; ++i) {
                    w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
                }

                uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
                for (int i = 0; i < 80; ++i) {
                    uint32_t f, k;
                    if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999; }
                    else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
                    else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
                    else { f = b ^ c ^ d; k = 0xCA62C1D6; }
                    uint32_t temp = rol(a, 5) + f + e + k + w[i];
                    e = d;
                    d = c;
                    c = rol(b, 30);
                    b = a;
                    a = temp;
                }
                h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
            }

            std::ostringstream result;
            result << std::hex << std::setfill('0');
            result << std::setw(8) << h0;
            result << std::setw(8) << h1;
            result << std::setw(8) << h2;
            result << std::setw(8) << h3;
            result << std::setw(8) << h4;
            return result.str();
        }
    };
}