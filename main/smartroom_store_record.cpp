#include "smartroom_store_record.h"

#include <cstring>

namespace smartroom {
namespace {
constexpr uint32_t kMagic = 0x53495231;

struct __attribute__((packed)) SignalRecordHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t stable_id;
    uint32_t carrier_hz;
    uint16_t count;
    uint16_t reserved;
    uint32_t crc32;
};

uint32_t crc32_sw(const uint8_t *data, size_t len, uint32_t seed)
{
    uint32_t crc = ~seed;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320U & (-(int)(crc & 1)));
        }
    }
    return ~crc;
}

uint32_t compute_crc(const SignalRecordHeader &header, const uint16_t *durations)
{
    SignalRecordHeader scratch = header;
    scratch.crc32 = 0;
    uint32_t crc = crc32_sw(reinterpret_cast<const uint8_t *>(&scratch), sizeof(scratch), 0);
    return crc32_sw(reinterpret_cast<const uint8_t *>(durations), header.count * sizeof(uint16_t), crc);
}
} // namespace

bool encode_record(const StoredSignalRecord &record, std::vector<uint8_t> &out_blob)
{
    if (record.signal.durations_us.empty() || record.signal.durations_us.size() > UINT16_MAX) {
        return false;
    }

    SignalRecordHeader header = {};
    header.magic = kMagic;
    header.version = record.version;
    header.flags = (record.signal.carrier_active_high ? 0x1 : 0x0) | (record.signal.level0 ? 0x2 : 0x0);
    header.stable_id = record.signal.stable_id;
    header.carrier_hz = record.signal.carrier_hz;
    header.count = record.signal.durations_us.size();

    out_blob.resize(sizeof(header) + record.signal.durations_us.size() * sizeof(uint16_t));
    memcpy(out_blob.data(), &header, sizeof(header));
    memcpy(out_blob.data() + sizeof(header), record.signal.durations_us.data(), record.signal.durations_us.size() * sizeof(uint16_t));

    auto *final_header = reinterpret_cast<SignalRecordHeader *>(out_blob.data());
    final_header->crc32 = compute_crc(*final_header, record.signal.durations_us.data());
    return true;
}

bool decode_record(const std::vector<uint8_t> &blob, StoredSignalRecord &out_record)
{
    if (blob.size() < sizeof(SignalRecordHeader)) {
        return false;
    }

    auto *header = reinterpret_cast<const SignalRecordHeader *>(blob.data());
    if (header->magic != kMagic) {
        return false;
    }

    size_t expected = sizeof(SignalRecordHeader) + header->count * sizeof(uint16_t);
    if (blob.size() != expected) {
        return false;
    }

    auto *dur = reinterpret_cast<const uint16_t *>(blob.data() + sizeof(SignalRecordHeader));
    if (compute_crc(*header, dur) != header->crc32) {
        return false;
    }

    out_record.version = header->version;
    out_record.signal.stable_id = header->stable_id;
    out_record.signal.carrier_hz = header->carrier_hz;
    out_record.signal.carrier_active_high = (header->flags & 0x1) != 0;
    out_record.signal.level0 = (header->flags & 0x2) != 0;
    out_record.signal.durations_us.assign(dur, dur + header->count);
    return true;
}

} // namespace smartroom
