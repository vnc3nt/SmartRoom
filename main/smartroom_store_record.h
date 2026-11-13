#pragma once

#include "smartroom_ir_codec.h"

#include <cstdint>
#include <vector>

namespace smartroom {

struct StoredSignalRecord {
    uint16_t version = 1;
    IrSignal signal;
};

bool encode_record(const StoredSignalRecord &record, std::vector<uint8_t> &out_blob);
bool decode_record(const std::vector<uint8_t> &blob, StoredSignalRecord &out_record);

} // namespace smartroom
