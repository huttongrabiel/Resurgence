#include "../navtypes.h"
#include "world_interface.h"

#include <memory>
#include <unistd.h>

// using namespace kinematics;
using kinematics::DiffDriveKinematics;
using kinematics::DiffWristKinematics;
using namespace navtypes;
using namespace robot::types;

namespace robot {

extern const WorldInterface WORLD_INTERFACE = WorldInterface::noop;

namespace {
DiffDriveKinematics drive_kinematics(1); // doesn't really matter what we set this to
DiffWristKinematics wrist_kinematics;
} // namespace

const DiffDriveKinematics& driveKinematics() {
	return drive_kinematics;
}

const DiffWristKinematics& wristKinematics() {
	return wrist_kinematics;
}

void world_interface_init(bool initMotorsOnly) {}

std::shared_ptr<robot::base_motor> getMotor(robot::types::motorid_t motor) {
	return nullptr;
}

void emergencyStop() {}

landmarks_t readLandmarks() {
	return landmarks_t{};
}

DataPoint<navtypes::eulerangles_t> readIMU() {
	return {};
}

DataPoint<pose_t> getTruePose() {
	return {};
}

DataPoint<pose_t> readVisualOdomVel() {
	return DataPoint<pose_t>{};
}

URCLeg getLeg(int /*id*/) {
	return URCLeg{-1, -1, {0., 0., 0.}};
}

void setMotorPower(motorid_t motor, double normalizedPWM) {}

void setMotorPos(motorid_t motor, int32_t targetPos) {}

types::DataPoint<int32_t> getMotorPos(robot::types::motorid_t motor) {
	return {};
}

void setMotorVel(robot::types::motorid_t motor, int32_t targetVel) {}

void setIndicator(indication_t signal) {}

callbackid_t addLimitSwitchCallback(
	robot::types::motorid_t motor,
	const std::function<void(robot::types::motorid_t motor,
							 robot::types::DataPoint<LimitSwitchData> limitSwitchData)>&
		callback) {
	return 0;
}

void removeLimitSwitchCallback(callbackid_t id) {}

} // namespace robot

DataPoint<gpscoords_t> gps::readGPSCoords() {
	return {};
}
