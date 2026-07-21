#include "voiceqas/rtp/depacketizer.hpp"

#include "voiceqas/rtp/g722_codec.hpp"
#include "voiceqas/rtp/g729_codec.hpp"

#include <cmath>
#include <cstring>

namespace voiceqas::rtp {

namespace {

inline int16_t ulaw_to_linear(uint8_t ulaw) {
    static const int16_t table[256] = {
        -32124,-31100,-30076,-29052,-28028,-27004,-25980,-24956,-23932,-22908,-21884,-20860,-19836,-18812,-17788,-16764,
        -15996,-15484,-14972,-14460,-13948,-13436,-12924,-12412,-11900,-11388,-10876,-10364, -9852, -9340, -8828, -8316,
         -7932, -7676, -7420, -7164, -6908, -6652, -6396, -6140, -5884, -5628, -5372, -5116, -4860, -4604, -4348, -4092,
         -3900, -3772, -3644, -3516, -3388, -3260, -3132, -3004, -2876, -2748, -2620, -2492, -2364, -2236, -2108, -1980,
         -1884, -1820, -1756, -1692, -1628, -1564, -1500, -1436, -1372, -1308, -1244, -1180, -1116, -1052,  -988,  -924,
          -876,  -844,  -812,  -780,  -748,  -716,  -684,  -652,  -620,  -588,  -556,  -524,  -492,  -460,  -428,  -396,
          -372,  -356,  -340,  -324,  -308,  -292,  -276,  -260,  -244,  -228,  -212,  -196,  -180,  -164,  -148,  -132,
          -120,  -112,  -104,   -96,   -88,   -80,   -72,   -64,   -56,   -48,   -40,   -32,   -24,   -16,    -8,     0,
          32124, 31100, 30076, 29052, 28028, 27004, 25980, 24956, 23932, 22908, 21884, 20860, 19836, 18812, 17788, 16764,
          15996, 15484, 14972, 14460, 13948, 13436, 12924, 12412, 11900, 11388, 10876, 10364,  9852,  9340,  8828,  8316,
           7932,  7676,  7420,  7164,  6908,  6652,  6396,  6140,  5884,  5628,  5372,  5116,  4860,  4604,  4348,  4092,
           3900,  3772,  3644,  3516,  3388,  3260,  3132,  3004,  2876,  2748,  2620,  2492,  2364,  2236,  2108,  1980,
           1884,  1820,  1756,  1692,  1628,  1564,  1500,  1436,  1372,  1308,  1244,  1180,  1116,  1052,   988,   924,
            876,   844,   812,   780,   748,   716,   684,   652,   620,   588,   556,   524,   492,   460,   428,   396,
            372,   356,   340,   324,   308,   292,   276,   260,   244,   228,   212,   196,   180,   164,   148,   132,
            120,   112,   104,    96,    88,    80,    72,    64,    56,    48,    40,    32,    24,    16,     8,     0};
    return table[ulaw];
}

inline int16_t alaw_to_linear(uint8_t alaw) {
    alaw ^= 0x55;
    int t = alaw & 0x7F;
    t ^= 0x55;
    int seg = (t & 0x70) >> 4;
    int mant = t & 0x0F;
    int16_t sample;
    if (seg == 0) {
        sample = static_cast<int16_t>((mant << 4) + 8);
    } else {
        sample = static_cast<int16_t>(((mant << 4) + 0x108) << (seg - 1));
    }
    return (alaw & 0x80) ? sample : static_cast<int16_t>(-sample);
}

uint16_t read_u16_be(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

uint32_t read_u32_be(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24)
         | (static_cast<uint32_t>(p[1]) << 16)
         | (static_cast<uint32_t>(p[2]) << 8)
         | static_cast<uint32_t>(p[3]);
}

}  // namespace

std::vector<int16_t> decode_g711_pcmu(std::span<const uint8_t> encoded) {
    std::vector<int16_t> out(encoded.size());
    for (size_t i = 0; i < encoded.size(); ++i) {
        out[i] = ulaw_to_linear(encoded[i]);
    }
    return out;
}

std::vector<int16_t> decode_g711_pcma(std::span<const uint8_t> encoded) {
    std::vector<int16_t> out(encoded.size());
    for (size_t i = 0; i < encoded.size(); ++i) {
        out[i] = alaw_to_linear(encoded[i]);
    }
    return out;
}

std::vector<int16_t> decode_g722_payload(std::span<const uint8_t> encoded) {
    return decode_g722(encoded);
}

std::vector<int16_t> decode_g729_payload(std::span<const uint8_t> encoded) {
    return decode_g729(encoded);
}

int rtp_clock_rate_for_payload(PayloadType pt) {
    switch (pt) {
        case PayloadType::G722:
            return 8000;
        case PayloadType::Pcmu:
        case PayloadType::Pcma:
        case PayloadType::G729:
        default:
            return 8000;
    }
}

std::optional<RtpHeader> parse_header(std::span<const uint8_t> packet) {
    if (packet.size() < 12) {
        return std::nullopt;
    }
    RtpHeader h;
    h.version = (packet[0] >> 6) & 0x03;
    if (h.version != 2) {
        return std::nullopt;
    }
    const uint8_t cc = packet[0] & 0x0F;
    h.payload_type = packet[1] & 0x7F;
    h.sequence = read_u16_be(packet.data() + 2);
    h.timestamp = read_u32_be(packet.data() + 4);
    h.ssrc = read_u32_be(packet.data() + 8);
    h.header_size = 12 + cc * 4;
    if (packet.size() < h.header_size) {
        return std::nullopt;
    }
    return h;
}

std::span<const uint8_t> payload(std::span<const uint8_t> packet, const RtpHeader& header) {
    return packet.subspan(header.header_size);
}

RtpDepacketizer::RtpDepacketizer(int rtp_clock_rate) : rtp_clock_rate_(rtp_clock_rate) {}

void RtpDepacketizer::reset() {
    has_sequence_ = false;
    has_timestamp_ = false;
    packets_received_ = 0;
    packets_lost_ = 0;
    jitter_ms_ = 0.0;
    g722_decoder_.reset();
    g729_decoder_.reset();
}

RtpDepacketizer::DecodeResult RtpDepacketizer::decode_packet(
    std::span<const uint8_t> packet,
    PayloadType expected) {
    DecodeResult result;
    auto header = parse_header(packet);
    if (!header) {
        return result;
    }

    if (header->payload_type != static_cast<uint8_t>(expected)) {
        return result;
    }

    ++packets_received_;
    if (has_sequence_) {
        const uint16_t expected_seq = static_cast<uint16_t>(last_sequence_ + 1);
        if (header->sequence != expected_seq) {
            const uint16_t gap = static_cast<uint16_t>(header->sequence - expected_seq);
            packets_lost_ += gap;
        }
    }
    last_sequence_ = header->sequence;
    has_sequence_ = true;

    if (has_timestamp_) {
        const int32_t delta = static_cast<int32_t>(header->timestamp - last_timestamp_);
        const double ms = std::abs(delta) * 1000.0 / static_cast<double>(rtp_clock_rate_);
        jitter_ms_ = jitter_ms_ + (ms - jitter_ms_) / 16.0;
    }
    last_timestamp_ = header->timestamp;
    has_timestamp_ = true;

    const auto pl = payload(packet, *header);
    switch (expected) {
        case PayloadType::Pcmu:
            result.pcm = decode_g711_pcmu(pl);
            break;
        case PayloadType::Pcma:
            result.pcm = decode_g711_pcma(pl);
            break;
        case PayloadType::G722:
            if (!g722_decoder_) {
                g722_decoder_ = std::make_unique<G722Decoder>();
            }
            result.pcm = g722_decoder_->decode(pl);
            break;
        case PayloadType::G729:
            if (!g729_decoder_) {
                g729_decoder_ = std::make_unique<G729Decoder>();
            }
            result.pcm = g729_decoder_->decode(pl);
            break;
    }

    const uint32_t total = packets_received_ + packets_lost_;
    result.stats.packet_loss_pct = total > 0
        ? 100.0 * static_cast<double>(packets_lost_) / static_cast<double>(total)
        : 0.0;
    result.stats.jitter_ms = jitter_ms_;
    result.stats.packets_received = packets_received_;
    result.stats.packets_lost = packets_lost_;
    return result;
}

}  // namespace voiceqas::rtp
