#include "MissionControlTasks.h"

#include "../Constants.h"
#include "../Globals.h"
#include "../utils/core.h"
#include "../world_interface/world_interface.h"
#include "MissionControlMessages.h"

#include <loguru.hpp>

#include <nlohmann/json.hpp>

using namespace robot::types;
using namespace std::chrono_literals;

using nlohmann::json;

namespace net::mc::tasks {
namespace {
const std::chrono::milliseconds TELEM_REPORT_PERIOD = 100ms;
}

PowerRepeatTask::PowerRepeatTask()
	: util::PeriodicTask<>(Constants::JOINT_POWER_REPEAT_PERIOD,
						   std::bind(&PowerRepeatTask::periodicTask, this)) {}

void PowerRepeatTask::setJointPower(jointid_t id, double power) {
	std::lock_guard lock(_mutex);
	_last_joint_power[id] = power;
}

void PowerRepeatTask::setCmdVel(double steerVel, double xVel) {
	std::lock_guard lock(_mutex);
	_last_cmd_vel = {steerVel, xVel};
}

void PowerRepeatTask::start() {
	// lock the mutex before start() to prevent data races between start() and stop()
	std::lock_guard lock(_mutex);
	util::PeriodicTask<>::start();
}

void PowerRepeatTask::stop() {
	std::lock_guard lock(_mutex);
	util::PeriodicTask<>::stop();
	_last_joint_power.clear();
	_last_cmd_vel.reset();
}

void PowerRepeatTask::periodicTask() {
	std::lock_guard lock(_mutex);
	for (const auto& current_pair : _last_joint_power) {
		const jointid_t& joint = current_pair.first;
		const double& power = current_pair.second;
		// no need to repeatedly send 0 power
		// this is also needed to make the zero calibration script work
		if (power != 0.0) {
			robot::setJointPower(joint, power);
		}
	}
	if (_last_cmd_vel) {
		if (_last_cmd_vel->first != 0.0 || _last_cmd_vel->second != 0.0) {
			robot::setCmdVel(_last_cmd_vel->first, _last_cmd_vel->second);
		}
	}
}

CameraStreamTask::CameraStreamTask(websocket::SingleClientWSServer& server)
	: util::AsyncTask<>("MCP_Stream"), _server(server) {}

void CameraStreamTask::openStream(const CameraID& cam, int fps) {
	std::lock_guard lock(_mutex);
	_open_streams[cam] = 0;
	_camera_encoders[cam] =
		std::make_shared<video::H264Encoder>(fps, Constants::video::H264_RF_CONSTANT);
}

void CameraStreamTask::closeStream(const CameraID& cam) {
	std::lock_guard lock(_mutex);
	_open_streams.erase(cam);
	_camera_encoders.erase(cam);
}

void CameraStreamTask::task(std::unique_lock<std::mutex>&) {
	while (isRunningInternal()) {
		{
			std::lock_guard lg(_mutex);
			// for all open streams, check if there is a new frame
			for (const auto& stream : _open_streams) {
				const CameraID& cam = stream.first;
				const uint32_t& frame_num = stream.second;
				if (robot::hasNewCameraFrame(cam, frame_num)) {
					// if there is a new frame, grab it
					auto camDP = robot::readCamera(cam);

					if (camDP) {
						auto data = camDP.getData();
						uint32_t& new_frame_num = data.second;
						cv::Mat frame = data.first;
						// update the previous frame number
						this->_open_streams[cam] = new_frame_num;
						const auto& encoder = this->_camera_encoders[cam];

						// convert frame to encoded data and send it
						auto data_vector = encoder->encode_frame(frame);
						json msg = {{"type", CAMERA_STREAM_REP_TYPE},
									{"camera", cam},
									{"data", data_vector}};
						_server.sendJSON(Constants::MC_PROTOCOL_NAME, msg);
					}
				}
			}
		}
		std::this_thread::yield();
	}
}

TelemReportTask::TelemReportTask(websocket::SingleClientWSServer& server)
	: util::PeriodicTask<>(TELEM_REPORT_PERIOD,
						   std::bind(&TelemReportTask::sendTelemetry, this)),
	  _server(server) {}

void TelemReportTask::sendTelemetry() {
	// send joint positions
	for (const auto& cur : robot::types::name_to_jointid) {
		robot::types::DataPoint<int32_t> jpos = robot::getJointPos(cur.second);
		if (jpos.isValid()) {
			auto jointNameFrznStr = cur.first;
			std::string jointNameStdStr(jointNameFrznStr.data(), jointNameFrznStr.size());
			json msg = {{"type", JOINT_POSITION_REP_TYPE},
						{"joint", jointNameStdStr},
						{"position", static_cast<double>(jpos.getData()) / 1000.0}};
			this->_server.sendJSON(Constants::MC_PROTOCOL_NAME, msg);
		}
	}

	// send rover position and orientation
	auto imu = robot::readIMU();
	auto gps = gps::readGPSCoords();
	LOG_F(2, "imu_valid=%s, gps_valid=%s", util::to_string(imu.isValid()).c_str(),
		  util::to_string(gps.isValid()).c_str());
	if (imu.isValid()) {
		Eigen::Quaterniond quat = imu.getData();
		double lon = 0, lat = 0;
		if (gps.isValid()) {
			lon = gps.getData().lon;
			lat = gps.getData().lat;
		}
		double imuRecency = util::durationToSec(dataclock::now() - imu.getTime());
		double recency = imuRecency;
		if (gps.isValid()) {
			double gpsRecency = util::durationToSec(dataclock::now() - gps.getTime());
			recency = std::max(recency, gpsRecency);
		}
		json msg = {{"type", ROVER_POS_REP_TYPE},
					{"orientW", quat.w()},
					{"orientX", quat.x()},
					{"orientY", quat.y()},
					{"orientZ", quat.z()},
					{"lon", lon},
					{"lat", lat},
					{"recency", recency}};
		_server.sendJSON(Constants::MC_PROTOCOL_NAME, msg);
	}
}

ArmIKTask::ArmIKTask(websocket::SingleClientWSServer& server)
	: util::PeriodicTask<>(Constants::ARM_IK_UPDATE_PERIOD,
						   std::bind(&ArmIKTask::updateArmIK, this)),
	  _server(server) {}

void ArmIKTask::updateArmIK() {
	DataPoint<navtypes::Vectord<Constants::arm::IK_MOTORS.size()>> armJointPositions =
		robot::getMotorPositionsRad(Constants::arm::IK_MOTORS);
	if (armJointPositions.isValid()) {
		navtypes::Vectord<Constants::arm::IK_MOTORS.size()> targetJointPositions =
			Globals::planarArmController.getCommand(dataclock::now(), armJointPositions);
		targetJointPositions /= M_PI / 180.0 / 1000.0; // convert from radians to millidegrees
		for (size_t i = 0; i < Constants::arm::IK_MOTORS.size(); i++) {
			robot::setMotorPos(Constants::arm::IK_MOTORS[i],
							   static_cast<int32_t>(targetJointPositions(i)));
		}
	}
}

} // namespace net::mc::tasks