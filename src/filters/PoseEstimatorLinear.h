#pragma once

#include "../navtypes.h"
#include "KalmanFilter.h"

#include <Eigen/Core>

namespace filters {

/**
 * @brief Uses a Kalman Filter to estimate the robot pose.
 *
 * This class uses a Kalman Filter to continuously estimate the pose of the robot in 2d space.
 * The tracked states are x, y, and heading. All of these states are in map space.
 * In most cases, you should probably use PoseEstimator instead. However, I don't think we
 * should delete this until we confirm that PoseEstimator works on a real robot.
 */
class [[deprecated]] PoseEstimatorLinear {
public:
	/**
	 * @brief Create a new Pose Estimator.
	 *
	 * @param stateStdDevs The standard deviations for each of the state elements.
	 * 					   This represents noise in the system model.
	 * @param measurementStdDevs The standard deviations for each of the measurement elements.
	 * 							 This represents noise in the measurements.
	 * @param dt The time in seconds between updates. Used to discretize the system model.
	 */
	PoseEstimatorLinear(const Eigen::Vector3d& stateStdDevs,
						const Eigen::Vector3d& measurementStdDevs, double dt);

	/**
	 * @brief Correct the pose estimation with measurement data.
	 *
	 * The measurement should be in the same space as the state.
	 *
	 * @param measurement The measurement to use to correct the filter, as a transform.
	 */
	void correct(const navtypes::transform_t& measurement);

	/**
	 * @brief Use the model to predict the next system state, given the current inputs.
	 *
	 * @param thetaVel Commanded rotational velocity.
	 * @param xVel Commanded x velocity.
	 */
	void predict(double thetaVel, double xVel);

	/**
	 * @brief Get the current estimate covariance matrix.
	 *
	 * This is an indication of the uncertainty of the pose estimate.
	 *
	 * @return The estimate covariance matrix, AKA the P matrix.
	 */
	Eigen::Matrix3d getEstimateCovarianceMat() const {
		return kf.getEstimateCovarianceMat();
	}

	/**
	 * @brief Reset the estimator.
	 *
	 * Sets the state estimate to the zero vector and resets the estimate covariance matrix.
	 */
	void reset() {
		reset(Eigen::Vector3d::Zero());
	}

	/**
	 * @brief Reset the estimator.
	 *
	 * Sets the state estimate to the supplied vector and resets the estimate covariance
	 * matrix.
	 *
	 * @param pose The pose to which the state estimate will be set.
	 */
	void reset(const navtypes::pose_t& pose);

	/**
	 * @brief Reset the estimator.
	 * 
	 * @param pose The pose to set as the new estimate.
	 * @param stdDevs The standard deviations in each axis of the state vector.
	 */
	void reset(const navtypes::pose_t& pose, const navtypes::pose_t& stdDevs);

	/**
	 * @brief Gets the current state estimate.
	 *
	 * @return The state estimate.
	 */
	navtypes::pose_t getPose() const {
		return kf.getState();
	}

private:
	KalmanFilter<3, 3, 3> kf;
	double dt;
};

} // namespace filters
