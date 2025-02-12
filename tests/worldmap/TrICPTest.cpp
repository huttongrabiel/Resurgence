#include "../../src/worldmap/TrICP.h"
#include "../../src/Util.h"

#include <Eigen/LU>
#include <iostream>

#include <catch2/catch.hpp>

#include "../../src/worldmap/GlobalMap.h"

using namespace navtypes;
using namespace util;

namespace
{
double rand(double low, double high)
{
	return low + (::rand() / (RAND_MAX / (high - low))); // NOLINT(cert-msc50-cpp)
}
} // namespace

TEST_CASE("Trimmed ICP")
{
	points_t map;
	points_t sample;
	points_t truths;
	transform_t trf = toTransformRotateFirst(0.1, -0.25, M_PI / 24);
	srand(time(nullptr)); // NOLINT(cert-msc51-cpp)
	for (int i = 0; i < 150; i++)
	{
		double x1 = rand(-6, 2);
		double y1 = pow(x1, 3);
		map.push_back({x1, y1, 1});

		double x2 = rand(-2, 6);
		double y2 = pow(x2, 3);
		point_t p = {x2, y2, 1};
		truths.push_back(p);
		p = trf * p;
		sample.push_back(p);
	}

	GlobalMap globalMap(1000);
	globalMap.addPoints(transform_t::Identity(), map, 1);

	TrICP icp(25, 0.005, std::bind(&GlobalMap::getClosest, &globalMap, std::placeholders::_1));
	// approximate the inverse of the transform used to create the sample
	transform_t trfApprox = icp.correct(sample, 0.3);
	transform_t trfInv = trf.inverse();

	double mse = (trfInv - trfApprox).array().square().mean();
	std::cout << "TrICP MSE: " << mse << std::endl;
	CHECK(mse == Approx(0).margin(0.01));
}
