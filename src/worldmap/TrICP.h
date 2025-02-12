#pragma once

#include "../navtypes.h"

/**
 * Implements the Trimmed Iterative Closest Point algorithm for 2D point cloud registration.
 *
 * @see <a
 * href="https://www.researchgate.net/publication/3974183_The_Trimmed_Iterative_Closest_Point_algorithm">Research
 * paper</a>
 */
class TrICP {
public:
	/**
	 * Creates a new TrICP object.
	 *
	 * @param maxIter Maximum number of iterations for point cloud registration.
	 * @param relErrChangeThresh Minimum relative error change threshold for registration.
	 * Iteration will terminate early if the relative change in error drops below this amount.
	 * @param getClosest A function that finds the closest point to a given point in the
	 * collection of points that the new point cloud sample is being registered to.
	 */
	explicit TrICP(int maxIter, double relErrChangeThresh,
				   std::function<navtypes::point_t(const navtypes::point_t&)> getClosest);
	/**
	 * Finds a rigid transformation that transforms the given sample to match the map.
	 *
	 * @param sample The sample to register.
	 * @param overlap The estimated overlap, in the range [0,1] between this sample and the
	 * global map. It's better to underestimate than overestimate. However, if it's too low
	 * then no meaningful features can be matched. Set to 0 to add points directly
	 * with no feature matching.
	 * @return A rigid transformation that transforms the given sample to match the map,
	 * or the identity matrix if the sample is empty or overlap is 0.
	 */
	navtypes::transform_t correct(const navtypes::points_t& sample, double overlap);

private:
	// maximum number of iterations to use
	int maxIter;
	// if the relative error change drops below this threshold, stop iterating
	double relErrChangeThresh;
	// find the closest point in the reference to the given point
	std::function<navtypes::point_t(const navtypes::point_t&)> getClosest;

	// performs a single iteration of point cloud registration, returning the computed
	// transform
	navtypes::transform_t iterate(navtypes::points_t& sample, int N, double& mse) const;

	// returns true iff any of the stopping conditions are met
	bool isDone(int numIter, double S, double oldMSE) const;
};
