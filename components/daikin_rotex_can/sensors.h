#pragma once

#include "esphome/components/daikin_rotex_can/daikin_rotex_can.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

namespace esphome {
namespace daikin_rotex_can {

class GenericSensor : public sensor::Sensor, public TRequest, public Parented<DaikinRotexCanComponent> {
public:
    GenericSensor() = default;

protected:
    virtual TVariant handleValue(uint16_t value) override;
};

class GenericTextSensor : public text_sensor::TextSensor, public TRequest, public Parented<DaikinRotexCanComponent> {
public:
    using TRecalculateState = std::function<std::string(EntityBase*, std::string const&)>;

    GenericTextSensor() = default;
    void set_recalculate_state(TRecalculateState&& lambda) { m_recalculate_state = std::move(lambda); }
protected:
    virtual TVariant handleValue(uint16_t value) override;
private:
    TRecalculateState m_recalculate_state;
};

class GenericBinarySensor : public binary_sensor::BinarySensor, public TRequest, public Parented<DaikinRotexCanComponent> {
public:
    GenericBinarySensor() = default;

protected:
    virtual TVariant handleValue(uint16_t value) override;
};

}  // namespace ld2410
}  // namespace esphome