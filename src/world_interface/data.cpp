#include "data.h"

namespace robot::types {

LimitSwitchData::LimitSwitchData(unsigned long long data) : data(data) {}

bool LimitSwitchData::isOpen(size_t idx) {
	return !data.test(idx);
}

bool LimitSwitchData::isClosed(size_t idx) {
	return data.test(idx);
}

bool LimitSwitchData::isAnyClosed() {
	return data.any();
}

bool LimitSwitchData::isAnyOpen() {
	return !data.all();
}

std::bitset<N_LIMIT_SWITCH> LimitSwitchData::diff(const LimitSwitchData& other) {
	return data ^ other.data;
}

constexpr float PotentiometerParams::scale() const {
	return (static_cast<int16_t>(adc_hi) - static_cast<int16_t>(adc_lo))/(mdeg_hi - mdeg_lo);
}
} // namespace robot::types

namespace util {

std::string to_string(robot::types::jointid_t joint) {
	using robot::types::jointid_t;
	switch (joint) {
		case jointid_t::armBase:
			return "armBase";
		case jointid_t::shoulder:
			return "shoulder";
		case jointid_t::elbow:
			return "elbow";
		case jointid_t::forearm:
			return "forearm";
		case jointid_t::hand:
			return "hand";
		case jointid_t::differentialPitch:
			return "differentialPitch";
		case jointid_t::differentialRoll:
			return "differentialRoll";
		case jointid_t::drill_arm:
			return "drillArm";
		default:
			// should never happen
			return "<unknown>";
	}
}
std::string to_string(const robot::types::CameraID& id) {
	return id;
}

} // namespace util