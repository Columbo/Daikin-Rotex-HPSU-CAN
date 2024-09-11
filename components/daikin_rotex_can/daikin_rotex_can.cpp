#include "esphome/components/daikin_rotex_can/daikin_rotex_can.h"
#include "esphome/components/daikin_rotex_can/request.h"
#include <string>
#include <vector>

namespace esphome {
namespace daikin_rotex_can {

static const char* TAG = "daikin_rotex_can";

DaikinRotexCanComponent::DaikinRotexCanComponent()
: m_accessor(this)
, m_data_requests()
, m_later_calls()
, m_dhw_run_lambdas()
{
}

void DaikinRotexCanComponent::setup() {
    ESP_LOGI(TAG, "setup");

    for (auto const& entity_conf : m_accessor.get_entities()) {
        const std::array<uint8_t, 7> data = Utils::str_to_bytes_array8(entity_conf.data);
        const std::array<uint16_t, 7> expected_response = Utils::str_to_bytes_array16(entity_conf.expected_response);

        m_data_requests.add({
            entity_conf.id,
            data,
            expected_response,
            entity_conf.pEntity,
            [entity_conf, this](auto const& data) -> DataType {
                DataType variant;

                if (entity_conf.data_offset > 0 && (entity_conf.data_offset + entity_conf.data_size) <= 7) {
                    if (entity_conf.data_size >= 1 && entity_conf.data_size <= 2) {
                        const float value = entity_conf.data_size == 2 ?
                            (((data[entity_conf.data_offset] << 8) + data[entity_conf.data_offset + 1]) / entity_conf.divider) :
                            (data[entity_conf.data_offset] / entity_conf.divider);

                        if (dynamic_cast<sensor::Sensor*>(entity_conf.pEntity) != nullptr) {
                            Utils::toSensor(entity_conf.pEntity)->publish_state(value);
                            variant = value;
                        } else if (dynamic_cast<binary_sensor::BinarySensor*>(entity_conf.pEntity) != nullptr) {
                            Utils::toBinarySensor(entity_conf.pEntity)->publish_state(value);
                            variant = value;
                        } else if (dynamic_cast<text_sensor::TextSensor*>(entity_conf.pEntity) != nullptr) {
                            auto it = entity_conf.map.findByKey(value);
                            const std::string str = it != entity_conf.map.end() ? it->second : "INVALID";
                            Utils::toTextSensor(entity_conf.pEntity)->publish_state(str);
                            variant = str;
                        } else if (dynamic_cast<select::Select*>(entity_conf.pEntity) != nullptr) {
                            auto it = entity_conf.map.findByKey(value);
                            const std::string str = it != entity_conf.map.end() ? it->second : "INVALID";
                            Utils::toSelect(entity_conf.pEntity)->publish_state(str);
                            variant = str;
                        } else if (dynamic_cast<number::Number*>(entity_conf.pEntity) != nullptr) {
                            Utils::toNumber(entity_conf.pEntity)->publish_state(value);
                            variant = value;
                        }
                    } else {
                        call_later([entity_conf](){
                            ESP_LOGE("validateConfig", "Invalid data size: %d", entity_conf.data_size);
                        });
                    }
                } else {
                    call_later([entity_conf](){
                        ESP_LOGE("validateConfig", "Invalid data_offset: %d", entity_conf.data_offset);
                    });
                }

                call_later([entity_conf, this](){
                    if (!entity_conf.update_entity.empty()) {
                        updateState(entity_conf.update_entity);
                    }
                });

                if (entity_conf.id == "target_hot_water_temperature") {
                    call_later([this](){
                        run_dhw_lambdas();
                    });
                }

                return variant;
            },
            [entity_conf](float const& value) -> std::vector<uint8_t> {
                return Utils::str_to_bytes(entity_conf.setter, value * entity_conf.divider);
            },
            !entity_conf.setter.empty(),
            entity_conf.update_interval
        });
    }

    m_data_requests.removeInvalidRequests();
}

void DaikinRotexCanComponent::updateState(std::string const& id) {
    if (id == "thermal_power") {
        update_thermal_power();
    }
}

void DaikinRotexCanComponent::update_thermal_power() {
    text_sensor::TextSensor* mode_of_operating = m_data_requests.get_text_sensor("mode_of_operating");
    sensor::Sensor* thermal_power = m_accessor.get_thermal_power();

    if (mode_of_operating != nullptr && thermal_power != nullptr) {
        sensor::Sensor* water_flow = m_data_requests.get_sensor("water_flow");
        sensor::Sensor* tvbh = m_data_requests.get_sensor("tvbh");
        sensor::Sensor* tv = m_data_requests.get_sensor("tv");
        sensor::Sensor* tr = m_data_requests.get_sensor("tr");

        float value = 0;
        if (mode_of_operating->state == "Warmwasserbereitung" && tv != nullptr && tr != nullptr && water_flow != nullptr) {
            value = (tv->state - tr->state) * (4.19 * water_flow->state) / 3600.0f;
        } else if ((mode_of_operating->state == "Heizen" || mode_of_operating->state == "Kühlen") && tvbh != nullptr && tr != nullptr && water_flow != nullptr) {
            value = (tvbh->state - tr->state) * (4.19 * water_flow->state) / 3600.0f;
        }
        thermal_power->publish_state(value);
    }
}

///////////////// Texts /////////////////
void DaikinRotexCanComponent::custom_request(std::string const& value) {
    const uint32_t can_id = 0x680;
    const bool use_extended_id = false;

    const std::vector<uint8_t> buffer = Utils::str_to_bytes(value);

    if (!buffer.empty()) {
        esphome::esp32_can::ESP32Can* pCanbus = m_data_requests.getCanbus();
        pCanbus->send_data(can_id, use_extended_id, { buffer.begin(), buffer.end() });

        Utils::log("sendGet", "can_id<%s> data<%s>",
            Utils::to_hex(can_id).c_str(), value.c_str(), Utils::to_hex(buffer).c_str());
    }
}

///////////////// Selects /////////////////
void DaikinRotexCanComponent::set_generic_select(std::string const& id, std::string const& state) {
    for (auto& entity_conf : m_accessor.get_entities()) {
        if (entity_conf.id == id && dynamic_cast<select::Select*>(entity_conf.pEntity) != nullptr) {
            auto it = entity_conf.map.findByValue(state);
            if (it != entity_conf.map.end()) {
                m_data_requests.sendSet(entity_conf.pEntity->get_name(), it->first);
            } else {
                ESP_LOGE(TAG, "set_generic_select(%s, %s) => Invalid value!", id.c_str(), state.c_str());
            }
            break;
        }
    }
}

///////////////// Numbers /////////////////
void DaikinRotexCanComponent::set_generic_number(std::string const& id, float value) {
    for (auto& entity_conf : m_accessor.get_entities()) {
        if (entity_conf.id == id && dynamic_cast<number::Number*>(entity_conf.pEntity) != nullptr) {
            m_data_requests.sendSet(entity_conf.pEntity->get_name(), value);
            break;
        }
    }
}

///////////////// Buttons /////////////////
void DaikinRotexCanComponent::dhw_run() {
    TRequest const* pRequest = m_data_requests.get("target_hot_water_temperature");
    if (pRequest != nullptr) {
        number::Number* pNumber = pRequest->get_number();
        if (pNumber != nullptr) {
            const float temp = pNumber->state;
            const std::string name = pRequest->getName();

            m_data_requests.sendSet(name, 70);

            call_later([name, temp, this](){
                m_data_requests.sendSet(name, temp);
            }, 10*1000);
        } else {
            ESP_LOGE("dhw_rum", "Request doesn't have a Number!");
        }
    } else {
        ESP_LOGE("dhw_rum", "Request couldn't be found!");
    }
}

void DaikinRotexCanComponent::run_dhw_lambdas() {
    if (m_accessor.getDaikinRotexCanComponent() != nullptr) {
        if (!m_dhw_run_lambdas.empty()) {
            auto& lambda = m_dhw_run_lambdas.front();
            lambda();
            m_dhw_run_lambdas.pop_front();
        }
    }
}

void DaikinRotexCanComponent::loop() {
    m_data_requests.sendNextPendingGet();
    for (auto it = m_later_calls.begin(); it != m_later_calls.end(); ) {
        if (millis() > it->second) {
            it->first();
            it = m_later_calls.erase(it);
        } else {
            ++it;
        }
    }
}

void DaikinRotexCanComponent::handle(uint32_t can_id, std::vector<uint8_t> const& data) {
    m_data_requests.handle(can_id, data);
}

void DaikinRotexCanComponent::dump_config() {
    ESP_LOGCONFIG(TAG, "DaikinRotexCanComponent");
}

} // namespace daikin_rotex_can
} // namespace esphome