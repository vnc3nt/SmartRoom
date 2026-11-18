#include "smartroom_store.h"

#include "smartroom_store_record.h"

#include <algorithm>
#include <cinttypes>
#include <string>

#include "esp_log.h"
#include "nvs.h"

namespace smartroom {
namespace {
static const char *TAG = "smartroom_store";
constexpr const char *kNamespace = "smartroom_ir";
constexpr const char *kIndexKey = "ids";

std::string key_for_signal(uint32_t id)
{
    char key[16] = {0};
    snprintf(key, sizeof(key), "s%08" PRIx32, id);
    return std::string(key);
}

esp_err_t open_rw(nvs_handle_t &h)
{
    return nvs_open(kNamespace, NVS_READWRITE, &h);
}
} // namespace

esp_err_t SignalStore::init()
{
    std::vector<uint32_t> ids;
    esp_err_t err = read_index_locked(ids);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        m_ids.clear();
        m_ready = true;
        return ESP_OK;
    }
    if (err == ESP_OK) {
        m_ids = std::move(ids);
        m_ready = true;
    }
    return err;
}

esp_err_t SignalStore::read_index_locked(std::vector<uint32_t> &ids)
{
    nvs_handle_t h;
    esp_err_t err = open_rw(h);
    if (err != ESP_OK) {
        return err;
    }

    size_t bytes = 0;
    err = nvs_get_blob(h, kIndexKey, nullptr, &bytes);
    if (err != ESP_OK) {
        nvs_close(h);
        return err;
    }
    if ((bytes % sizeof(uint32_t)) != 0) {
        nvs_close(h);
        return ESP_ERR_INVALID_SIZE;
    }

    ids.resize(bytes / sizeof(uint32_t));
    err = nvs_get_blob(h, kIndexKey, ids.data(), &bytes);
    nvs_close(h);
    return err;
}

esp_err_t SignalStore::write_index_locked(const std::vector<uint32_t> &ids)
{
    nvs_handle_t h;
    esp_err_t err = open_rw(h);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(h, kIndexKey, ids.data(), ids.size() * sizeof(uint32_t));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t SignalStore::load_all(std::vector<IrSignal> &out_signals)
{
    if (!m_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    out_signals.clear();
    nvs_handle_t h;
    esp_err_t err = open_rw(h);
    if (err != ESP_OK) {
        return err;
    }

    for (uint32_t id : m_ids) {
        std::string key = key_for_signal(id);
        size_t bytes = 0;
        err = nvs_get_blob(h, key.c_str(), nullptr, &bytes);
        if (err != ESP_OK) {
            continue;
        }

        std::vector<uint8_t> blob(bytes);
        err = nvs_get_blob(h, key.c_str(), blob.data(), &bytes);
        if (err != ESP_OK) {
            continue;
        }

        StoredSignalRecord rec;
        if (!decode_record(blob, rec)) {
            ESP_LOGW(TAG, "Skipping invalid record id=%" PRIu32, id);
            continue;
        }
        out_signals.push_back(std::move(rec.signal));
    }

    nvs_close(h);
    return ESP_OK;
}

esp_err_t SignalStore::upsert(const IrSignal &signal)
{
    if (!m_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    StoredSignalRecord rec = {
        .version = 1,
        .signal = signal,
    };
    std::vector<uint8_t> blob;
    if (!encode_record(rec, blob)) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t h;
    esp_err_t err = open_rw(h);
    if (err != ESP_OK) {
        return err;
    }

    std::string key = key_for_signal(signal.stable_id);
    err = nvs_set_blob(h, key.c_str(), blob.data(), blob.size());
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        return err;
    }

    if (std::find(m_ids.begin(), m_ids.end(), signal.stable_id) == m_ids.end()) {
        m_ids.push_back(signal.stable_id);
    }
    return write_index_locked(m_ids);
}

esp_err_t SignalStore::erase(uint32_t stable_id)
{
    if (!m_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t h;
    esp_err_t err = open_rw(h);
    if (err != ESP_OK) {
        return err;
    }

    std::string key = key_for_signal(stable_id);
    err = nvs_erase_key(h, key.c_str());
    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        return err;
    }

    m_ids.erase(std::remove(m_ids.begin(), m_ids.end(), stable_id), m_ids.end());
    return write_index_locked(m_ids);
}

esp_err_t SignalStore::clear_all()
{
    if (!m_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    nvs_handle_t h;
    esp_err_t err = open_rw(h);
    if (err != ESP_OK) {
        return err;
    }

    for (uint32_t id : m_ids) {
        std::string key = key_for_signal(id);
        nvs_erase_key(h, key.c_str());
    }
    nvs_commit(h);
    nvs_close(h);

    m_ids.clear();
    return write_index_locked(m_ids);
}

size_t SignalStore::size() const
{
    return m_ids.size();
}

} // namespace smartroom
