#pragma once

#include <Eigen/Core>

#include "../simulator/utils.h"
#include "KalmanFilter.h"

/**
 * This class uses a Kalman Filter to continuously estimate the pose of the robot in 2d space.
 * The tracked states are x, y, and heading. All of these states are in map space.
 */
class PoseEstimator
{
public:
	/**
	 * Create a new Pose Estimator.
	 *
	 * @param stateStdDevs The standard deviations for each of the state elements.
	 * 					   This represents noise in the system model.
	 * @param measurementStdDevs The standard deviations for each of the measurement elements.
	 * 							 This represents noise in the measurements.
	 * @param dt The time in seconds between updates. Used to discretize the system model.
	 */
	PoseEstimator(const Eigen::Vector3d &stateStdDevs,
				  const Eigen::Vector3d &measurementStdDevs, double dt);

	/**
	 * Correct the pose estimation with measurement data.
	 * The measurement should be in the same space as the state.
	 *
	 * @param measurement The measurement to use to correct the filter, as a transform.
	 */
	void correct(const transform_t &measurement);

	/**
	 * Use the model to predict the next system state, given the current inputs.
	 *
	 * @param thetaVel Commanded rotational velocity.
	 * @param xVel Commanded x velocity.
	 */
	void predict(double thetaVel, double xVel);

	/**
	 * Get the current estimate covariance matrix.
	 * This is an indication of the uncertainty of the pose estimate.
	 *
	 * @return The estimate covariance matrix, AKA the P matrix.
	 */
	Eigen::Matrix3d getEstimateCovarianceMat() const;

	/**
	 * Sets the state estimate to the zero vector and resets the estimate covariance matrix.
	 */
	void reset()
	{
		reset(Eigen::Vector3d::Zero());
	}

	/**
	 * Sets the state estimate to the supplied vector and resets the estimate covariance
	 * matrix.
	 *
	 * @param pose The pose to which the state estimate will be set.
	 */
	void reset(const pose_t &pose);

	/**
	 * Gets the current state estimate.
	 *
	 * @return The state estimate.
	 */
	pose_t getPose() const;

private:
	KalmanFilter<3,3> kf;
	double dt;
};