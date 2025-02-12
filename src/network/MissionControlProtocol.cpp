#include "MissionControlProtocol.h"

#include "../Constants.h"
#include "../Globals.h"
#include "../base64/base64_img.h"
#include "../log.h"
#include "../utils/core.h"
#include "../utils/json.h"
#include "../world_interface/data.h"
#include "../world_interface/world_interface.h"

#include <chrono>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

using namespace robot::types;
using namespace std::chrono_literals;

namespace net {
namespace mc {

using val_t = json::value_t;
using robot::types::mountedperipheral_t;
using std::placeholders::_1;
using websocket::connhandler_t;
using websocket::msghandler_t;
using websocket::validator_t;

const std::chrono::milliseconds TELEM_REPORT_PERIOD = 100ms;
const std::chrono::milliseconds HEARTBEAT_TIMEOUT_PERIOD = 3000ms;

// TODO: possibly use frozen::string for this so we don't have to use raw char ptrs
// request keys
constexpr const char* EMERGENCY_STOP_REQ_TYPE = "emergencyStopRequest";
constexpr const char* OPERATION_MODE_REQ_TYPE = "operationModeRequest";
constexpr const char* DRIVE_REQ_TYPE = "driveRequest";
constexpr const char* ARM_IK_ENABLED_TYPE = "setArmIKEnabled";
constexpr const char* MOTOR_POWER_REQ_TYPE = "motorPowerRequest";
constexpr const char* JOINT_POWER_REQ_TYPE = "jointPowerRequest";
constexpr const char* MOTOR_POSITION_REQ_TYPE = "motorPositionRequest";
constexpr const char* JOINT_POSITION_REQ_TYPE = "jointPositionRequest";
constexpr const char* CAMERA_STREAM_OPEN_REQ_TYPE = "cameraStreamOpenRequest";
constexpr const char* CAMERA_STREAM_CLOSE_REQ_TYPE = "cameraStreamCloseRequest";

// report keys
constexpr const char* MOTOR_STATUS_REP_TYPE = "motorStatusReport";
constexpr const char* CAMERA_STREAM_REP_TYPE = "cameraStreamReport";
constexpr const char* LIDAR_REP_TYPE = "lidarReport";
constexpr const char* MOUNTED_PERIPHERAL_REP_TYPE = "mountedPeripheralReport";
constexpr const char* JOINT_POSITION_REP_TYPE = "jointPositionReport";
constexpr const char* ROVER_POS_REP_TYPE = "roverPositionReport";
// TODO: add support for missing report types
// autonomousPlannedPathReport, poseConfidenceReport

/**
   Set power for all joints to zero.
 */
static void stopAllJoints();

/*///////////////// VALIDATORS/HANDLERS ////////////////////

  For each protocol message type, there is a pair of functions in this section: a validator and
  a handler. The validator will return a boolean depending on whether or not a message is valid
  for this type, and the handler will perform the required actions for dealing with a message
  of the type. NOTE: handlers expect valid messages, so call validator first
*/

static bool validateEmergencyStopRequest(const json& j) {
	return util::validateKey(j, "stop", val_t::boolean);
}

void MissionControlProtocol::handleEmergencyStopRequest(const json& j) {
	bool stop = j["stop"];
	if (stop) {
		this->stopAndShutdownPowerRepeat();
		robot::emergencyStop();
		log(LOG_ERROR, "Emergency stop!\n");
	} else if (!Globals::AUTONOMOUS) {
		// if we are leaving e-stop (and NOT in autonomous), restart the power repeater
		this->startPowerRepeat();
	}
	// TODO: reinit motors
	Globals::E_STOP = stop;
	Globals::armIKEnabled = false;
}

static bool validateOperationModeRequest(const json& j) {
	return util::validateKey(j, "mode", val_t::string) &&
		   util::validateOneOf(j, "mode", {"teleoperation", "autonomous"});
}

void MissionControlProtocol::handleOperationModeRequest(const json& j) {
	std::string mode = j["mode"];
	Globals::AUTONOMOUS = (mode == "autonomous");
	if (Globals::AUTONOMOUS) {
		// if we have entered autonomous mode, we need to stop all the power repeater stuff.
		this->stopAndShutdownPowerRepeat();
	} else {
		// if we have left autonomous mode, we need to start the power repeater again.
		this->startPowerRepeat();
	}
}

static bool validateDriveRequest(const json& j) {
	std::string msg = j.dump();
	return util::hasKey(j, "straight") && util::validateRange(j, "straight", -1, 1) &&
		   util::hasKey(j, "steer") && util::validateRange(j, "steer", -1, 1);
}

static bool validateArmIKEnable(const json& j) {
	return util::validateKey(j, "enabled", val_t::boolean);
}

void MissionControlProtocol::handleDriveRequest(const json& j) {
	// TODO: ignore this message if we are in autonomous mode.
	// fit straight and steer to unit circle; i.e. if |<straight, steer>| > 1, scale each
	// component such that <straight, steer> is a unit vector.
	double straight = j["straight"];
	double steer = -j["steer"].get<double>();
	double norm = std::sqrt(std::pow(straight, 2) + std::pow(steer, 2));
	double dx = Constants::MAX_WHEEL_VEL * (norm > 1 ? straight / norm : straight);
	double dtheta = Constants::MAX_DTHETA * (norm > 1 ? steer / norm : steer);
	log(LOG_TRACE, "{straight=%.2f, steer=%.2f} -> setCmdVel(%.4f, %.4f)\n", straight, steer,
		dtheta, dx);
	this->setRequestedCmdVel(dtheta, dx);
}

void MissionControlProtocol::handleSetArmIKEnabled(const json& j) {
	bool enabled = j["enabled"];
	if (enabled) {
		Globals::armIKEnabled = false;
		if (_arm_ik_repeat_thread.joinable()) {
			_arm_ik_repeat_thread.join();
		}
		DataPoint<navtypes::Vectord<Constants::arm::IK_MOTORS.size()>> armJointPositions =
			robot::getMotorPositionsRad(Constants::arm::IK_MOTORS);
		// TODO: there should be a better way of handling invalid data than crashing.
		// It should somehow just not enable IK, but then it needs to communicate back to MC
		// that IK wasn't enabled?
		assert(armJointPositions.isValid());
		Globals::planarArmController.set_setpoint(armJointPositions.getData());
		Globals::armIKEnabled = true;
		_arm_ik_repeat_thread =
			std::thread(&MissionControlProtocol::updateArmIKRepeatTask, this);
	} else {
		Globals::armIKEnabled = false;
		if (_arm_ik_repeat_thread.joinable()) {
			_arm_ik_repeat_thread.join();
		}
	}
}

void MissionControlProtocol::setRequestedCmdVel(double dtheta, double dx) {
	{
		std::lock_guard<std::mutex> power_lock(this->_joint_power_mutex);
		this->_last_cmd_vel = {dtheta, dx};
	}
	robot::setCmdVel(dtheta, dx);
}

static bool validateJoint(const json& j) {
	return util::validateKey(j, "joint", val_t::string) &&
		   std::any_of(all_jointid_t.begin(), all_jointid_t.end(), [&](const auto& joint) {
			   return j["joint"].get<std::string>() == util::to_string(joint);
		   });
}

static bool validateJointPowerRequest(const json& j) {
	return validateJoint(j) && util::validateRange(j, "power", -1, 1);
}

void MissionControlProtocol::handleJointPowerRequest(const json& j) {
	// TODO: ignore this message if we are in autonomous mode.
	using robot::types::jointid_t;
	using robot::types::name_to_jointid;
	std::string joint = j["joint"];
	double power = j["power"];
	auto it = name_to_jointid.find(util::freezeStr(joint));
	if (it != name_to_jointid.end()) {
		jointid_t joint_id = it->second;
		setRequestedJointPower(joint_id, power);
	}
}

void MissionControlProtocol::sendRoverPos() {
	auto imu = robot::readIMU();
	auto gps = gps::readGPSCoords();
	log(LOG_DEBUG, "imu_valid=%s, gps_valid=%s\n", util::to_string(imu.isValid()).c_str(),
		util::to_string(gps.isValid()).c_str());
	if (imu.isValid()) {
		auto rpy = imu.getData();
		Eigen::Quaterniond quat(Eigen::AngleAxisd(rpy.roll, Eigen::Vector3d::UnitX()) *
								Eigen::AngleAxisd(rpy.pitch, Eigen::Vector3d::UnitY()) *
								Eigen::AngleAxisd(rpy.yaw, Eigen::Vector3d::UnitZ()));
		double posX = 0, posY = 0;
		if (gps.isValid()) {
			posX = gps.getData().lon;
			posY = gps.getData().lat;
		}
		double posZ = 0.0;
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
					{"posX", posX},
					{"posY", posY},
					{"posZ", posZ},
					{"recency", recency}};
		this->_server.sendJSON(Constants::MC_PROTOCOL_NAME, msg);
	}
}

static bool validateJointPositionRequest(const json& j) {
	return validateJoint(j) && util::validateKey(j, "position", val_t::number_integer);
}

static void handleJointPositionRequest(const json& j) {
	// TODO: ignore this message if we are in autonomous mode.
	std::string motor = j["joint"];
	double position_deg = j["position"];
	int32_t position_mdeg = std::round(position_deg * 1000);
	// TODO: actually implement joint position requests
	// setMotorPos(motor, position_mdeg);
}

static bool validateCameraStreamOpenRequest(const json& j) {
	return util::validateKey(j, "camera", val_t::string);
}

void MissionControlProtocol::handleCameraStreamOpenRequest(const json& j) {
	CameraID cam = j["camera"];
	std::unordered_set<CameraID> supported_cams = robot::getCameras();
	if (supported_cams.find(cam) != supported_cams.end()) {
		std::unique_lock<std::shared_mutex> stream_lock(this->_stream_mutex);
		this->_open_streams[cam] = 0;
		this->_camera_encoders[cam] =
			std::make_shared<video::H264Encoder>(j["fps"], Constants::video::H264_RF_CONSTANT);
	}
}

static bool validateCameraStreamCloseRequest(const json& j) {
	return util::validateKey(j, "camera", val_t::string);
}

void MissionControlProtocol::handleCameraStreamCloseRequest(const json& j) {
	CameraID cam = j["camera"];
	std::unique_lock<std::shared_mutex> stream_lock(this->_stream_mutex);
	this->_open_streams.erase(cam);
	this->_camera_encoders.erase(cam);
}

void MissionControlProtocol::sendJointPositionReport(const std::string& jointName,
													 int32_t jointPos) {
	json msg = {{"type", JOINT_POSITION_REP_TYPE},
				{"joint", jointName},
				{"position", static_cast<double>(jointPos) / 1000.0}};
	this->_server.sendJSON(Constants::MC_PROTOCOL_NAME, msg);
}

void MissionControlProtocol::sendCameraStreamReport(
	const CameraID& cam, const std::vector<std::basic_string<uint8_t>>& videoData) {
	json msg = {{"type", CAMERA_STREAM_REP_TYPE}, {"camera", cam}, {"data", videoData}};
	this->_server.sendJSON(Constants::MC_PROTOCOL_NAME, msg);
}

void MissionControlProtocol::handleConnection() {
	// Turn off inverse kinematics on connection
	Globals::armIKEnabled = false;

	// TODO: send the actual mounted peripheral, as specified by the command-line parameter
	json j = {{"type", MOUNTED_PERIPHERAL_REP_TYPE}};

	if (Globals::mountedPeripheral == mountedperipheral_t::none) {
		j["peripheral"] = json(nullptr);
	} else {
		j["peripheral"] = util::to_string(Globals::mountedPeripheral);
	}

	this->_server.sendJSON(Constants::MC_PROTOCOL_NAME, j);

	if (!Globals::AUTONOMOUS) {
		// start power repeat thread (if not already running)
		this->startPowerRepeat();
	}
}

void MissionControlProtocol::handleHeartbeatTimedOut() {
	this->stopAndShutdownPowerRepeat();
	robot::emergencyStop();
	log(LOG_ERROR, "Heartbeat timed out! Emergency stopping.\n");
	Globals::E_STOP = true;
	Globals::armIKEnabled = false;
}

void MissionControlProtocol::startPowerRepeat() {
	// note: take care to lock mutexes in a consistent order
	std::lock_guard<std::mutex> flagLock(_joint_repeat_running_mutex);
	std::lock_guard<std::mutex> threadLock(_joint_repeat_thread_mutex);
	if (!this->_joint_repeat_running) {
		this->_joint_repeat_running = true;
		this->_joint_repeat_thread =
			std::thread(&MissionControlProtocol::jointPowerRepeatTask, this);
	}
}

void MissionControlProtocol::stopAndShutdownPowerRepeat() {
	// check to make sure the thread is actually running first (so we don't stop everything
	// unnecessarily if it isn't; this could be bad in the case where we receive a spurious
	// operation mode request while already in autonomous and shut down all the motors)
	// note: take care to lock mutexes in a consistent order
	std::unique_lock<std::mutex> power_repeat_lock(_joint_repeat_running_mutex);
	if (this->_joint_repeat_running) {
		// Clear the last_joint_power map so the repeater thread won't do anything
		{
			std::lock_guard<std::mutex> joint_lock(this->_joint_power_mutex);
			this->_last_joint_power.clear();
			this->_last_cmd_vel = {};
		}
		// explicitly set all joints to zero
		stopAllJoints();
		// explicitly stop chassis
		robot::setCmdVel(0, 0);
		// shut down the power repeat thread
		this->_joint_repeat_running = false;
		// release before joining to prevent deadlock
		power_repeat_lock.unlock();
		_power_repeat_cv.notify_one();

		std::lock_guard<std::mutex> threadLock(_joint_repeat_thread_mutex);
		if (this->_joint_repeat_thread.joinable()) {
			this->_joint_repeat_thread.join();
		}
	}
	// Turn off inverse kinematics so that IK state will be in sync with mission control
	Globals::armIKEnabled = false;
}

MissionControlProtocol::MissionControlProtocol(SingleClientWSServer& server)
	: WebSocketProtocol(Constants::MC_PROTOCOL_NAME), _server(server), _open_streams(),
	  _last_joint_power(), _joint_repeat_running(false) {
	// TODO: Add support for tank drive requests
	// TODO: add support for science station requests (lazy susan, lazy susan lid, drill,
	// syringe)

	// emergency stop and operation mode handlers need the class for context since they must
	// be able to access the methods to start and stop the power repeater thread
	this->addMessageHandler(
		EMERGENCY_STOP_REQ_TYPE,
		std::bind(&MissionControlProtocol::handleEmergencyStopRequest, this, _1),
		validateEmergencyStopRequest);
	this->addMessageHandler(
		OPERATION_MODE_REQ_TYPE,
		std::bind(&MissionControlProtocol::handleOperationModeRequest, this, _1),
		validateOperationModeRequest);
	// drive and joint power handlers need the class for context since they must modify
	// _last_joint_power and _last_cmd_vel (for the repeater thread)
	this->addMessageHandler(DRIVE_REQ_TYPE,
							std::bind(&MissionControlProtocol::handleDriveRequest, this, _1),
							validateDriveRequest);
	this->addMessageHandler(
		ARM_IK_ENABLED_TYPE,
		std::bind(&MissionControlProtocol::handleSetArmIKEnabled, this, _1),
		validateArmIKEnable);
	this->addMessageHandler(
		ARM_IK_ENABLED_TYPE,
		std::bind(&MissionControlProtocol::handleSetArmIKEnabled, this, _1),
		validateArmIKEnable);
	this->addMessageHandler(
		JOINT_POWER_REQ_TYPE,
		std::bind(&MissionControlProtocol::handleJointPowerRequest, this, _1),
		validateJointPowerRequest);
	this->addMessageHandler(JOINT_POSITION_REQ_TYPE, handleJointPositionRequest,
							validateJointPositionRequest);
	// camera stream handlers need the class for context since they must modify _open_streams
	this->addMessageHandler(
		CAMERA_STREAM_OPEN_REQ_TYPE,
		std::bind(&MissionControlProtocol::handleCameraStreamOpenRequest, this, _1),
		validateCameraStreamOpenRequest);
	this->addMessageHandler(
		CAMERA_STREAM_CLOSE_REQ_TYPE,
		std::bind(&MissionControlProtocol::handleCameraStreamCloseRequest, this, _1),
		validateCameraStreamCloseRequest);
	this->addConnectionHandler(std::bind(&MissionControlProtocol::handleConnection, this));
	this->addDisconnectionHandler(
		std::bind(&MissionControlProtocol::stopAndShutdownPowerRepeat, this));

	this->setHeartbeatTimedOutHandler(
		HEARTBEAT_TIMEOUT_PERIOD,
		std::bind(&MissionControlProtocol::handleHeartbeatTimedOut, this));

	this->_streaming_running = true;
	this->_streaming_thread = std::thread(&MissionControlProtocol::videoStreamTask, this);

	// Joint position reporting
	this->_joint_report_thread = std::thread(&MissionControlProtocol::telemReportTask, this);
}

MissionControlProtocol::~MissionControlProtocol() {
	this->stopAndShutdownPowerRepeat();

	this->_streaming_running = false;
	if (this->_streaming_thread.joinable()) {
		this->_streaming_thread.join();
	}
}

void MissionControlProtocol::setRequestedJointPower(jointid_t joint, double power) {
	std::lock_guard<std::mutex> joint_lock(this->_joint_power_mutex);
	this->_last_joint_power[joint] = power;
	robot::setJointPower(joint, power);
}

void MissionControlProtocol::jointPowerRepeatTask() {
	std::unique_lock<std::mutex> joint_repeat_lock(this->_joint_repeat_running_mutex);
	while (this->_joint_repeat_running) {
		{
			std::lock_guard<std::mutex> joint_lock(this->_joint_power_mutex);
			for (const auto& current_pair : this->_last_joint_power) {
				if (!this->_joint_repeat_running) {
					break;
				}
				const jointid_t& joint = current_pair.first;
				const double& power = current_pair.second;
				// no need to repeatedly send 0 power
				// this is also needed to make the zero calibration script work
				if (power != 0.0) {
					robot::setJointPower(joint, power);
				}
			}
			if (this->_last_cmd_vel) {
				robot::setCmdVel(this->_last_cmd_vel->first, this->_last_cmd_vel->second);
			}
		}
		_power_repeat_cv.wait_for(joint_repeat_lock, Constants::JOINT_POWER_REPEAT_PERIOD,
								  [this] { return !_joint_repeat_running; });
	}
}

void MissionControlProtocol::updateArmIKRepeatTask() {
	dataclock::time_point next_update_time = dataclock::now();
	while (Globals::armIKEnabled) {
		DataPoint<navtypes::Vectord<Constants::arm::IK_MOTORS.size()>> armJointPositions =
			robot::getMotorPositionsRad(Constants::arm::IK_MOTORS);
		if (armJointPositions.isValid()) {
			navtypes::Vectord<Constants::arm::IK_MOTORS.size()> targetJointPositions =
				Globals::planarArmController.getCommand(dataclock::now(), armJointPositions);
			targetJointPositions /=
				M_PI / 180.0 / 1000.0; // convert from radians to millidegrees
			for (size_t i = 0; i < Constants::arm::IK_MOTORS.size(); i++) {
				robot::setMotorPos(Constants::arm::IK_MOTORS[i],
								   static_cast<int32_t>(targetJointPositions(i)));
			}
		}
		next_update_time += Constants::ARM_IK_UPDATE_PERIOD;
		std::this_thread::sleep_until(next_update_time);
	}
}

void MissionControlProtocol::telemReportTask() {
	dataclock::time_point pt = dataclock::now();

	while (true) {
		for (const auto& cur : robot::types::name_to_jointid) {
			robot::types::DataPoint<int32_t> jpos = robot::getJointPos(cur.second);
			if (jpos.isValid()) {
				auto jointNameFrznStr = cur.first;
				std::string jointNameStdStr(jointNameFrznStr.data(), jointNameFrznStr.size());
				sendJointPositionReport(jointNameStdStr, jpos.getData());
			}
		}

		sendRoverPos();

		pt += TELEM_REPORT_PERIOD;
		std::this_thread::sleep_until(pt);
	}
}

void MissionControlProtocol::videoStreamTask() {
	while (this->_streaming_running) {
		std::shared_lock<std::shared_mutex> stream_lock(this->_stream_mutex);
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
					sendCameraStreamReport(cam, data_vector);
				}
			}

			// break out of the loop if we should stop streaming
			if (!this->_streaming_running) {
				break;
			}
		}
		std::this_thread::yield();
	}
}

///// UTILITY FUNCTIONS //////

static void stopAllJoints() {
	for (jointid_t current : robot::types::all_jointid_t) {
		robot::setJointPower(current, 0.0);
	}
}

} // namespace mc
} // namespace net
