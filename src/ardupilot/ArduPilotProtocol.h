#pragma once

#include "../network/websocket/WebSocketProtocol.h"
#include "../network/websocket/WebSocketServer.h"
#include "../world_interface/data.h"

#include <mutex>

namespace net {
namespace ardupilot {

/* @brief A protocol in order to handle messages from an ArduPilot enabled device */
class ArduPilotProtocol {
public:
	ArduPilotProtocol(net::websocket::SingleClientWSServer& websocketServer);
	~ArduPilotProtocol();
	ArduPilotProtocol(const ArduPilotProtocol& other) = delete;
	ArduPilotProtocol& operator=(const ArduPilotProtocol& other) = delete;

	/* @brief Gets the last reported GPS position (lat, lon)
	 *
	 * @return Last reported GPS position
	 */
	robot::types::DataPoint<navtypes::gpscoords_t> getGPS();

	/* @brief Gets the last reported IMU readings (roll, pitch, yaw)
	 *
	 * @return Last reported IMU readings
	 */
	robot::types::DataPoint<navtypes::eulerangles_t> getIMU();

	/* @brief Gets the last reported heading (degrees)
	 *
	 * @return Last reported heading
	 */
	robot::types::DataPoint<int> getHeading();

private:
	/* @brief Validates GPS request
	 *
	 * @param json message
	 *
	 * @return True if message is valid, false otherwise
	 */
	static bool validateGPSRequest(const nlohmann::json& j);

	/* @brief Validates IMU request
	 *
	 * @param json message
	 *
	 * @return True if message is valid, false otherwise
	 */
	static bool validateIMURequest(const nlohmann::json& j);

	/* @brief Validates heading request
	 *
	 * @param json message
	 *
	 * @return True if message is valid, false otherwise
	 */
	static bool validateHeadingRequest(const nlohmann::json& j);

	/* @brief Destructures GPS json message and updates last reported GPS values
	 *
	 * @param json message
	 */
	void handleGPSRequest(const nlohmann::json& j);

	/* @brief Destructures IMU json message and updates last reported GPS values
	 *
	 * @param json message
	 */
	void handleIMURequest(const nlohmann::json& j);

	/* @brief Destructures heading json message and updates last reported GPS values
	 *
	 * @param json message
	 */
	void handleHeadingRequest(const nlohmann::json& j);

	/* @brief Checks if the ArduPilotProtocol is connected
	 *
	 * @return True if connected, false if not connected
	 */
	bool isArduPilotConnected();

	/* @brief Registers the protocol with the websocket server
	 *
	 * @param websocket server of which to add protocol
	 */
	void initArduPilotServer(net::websocket::SingleClientWSServer& websocketServer);

	/* @brief Connection handler for websocket server */
	void clientConnected();

	/* @brief Disconnect handler for websocket server */
	void clientDisconnected();

	std::mutex _connectionMutex;
	bool _arduPilotProtocolConnected;

	std::mutex _lastHeadingMutex;
	robot::types::DataPoint<int> _lastHeading;

	std::mutex _lastGPSMutex;
	robot::types::DataPoint<navtypes::gpscoords_t> _lastGPS;

	std::mutex _lastOrientationMutex;
	robot::types::DataPoint<navtypes::eulerangles_t> _lastOrientation;
};

} // namespace ardupilot
} // namespace net
