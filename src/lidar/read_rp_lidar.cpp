#include "read_rp_lidar.h"

#include "../navtypes.h"
#include "../world_interface/world_interface.h"
#include "../Constants.h"
#include "PointCloudProcessing.h"
#include "PointGenerator.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <iostream>

using namespace navtypes;

bool lidar_initialized = false;
RPLidar rp_lidar(Constants::Lidar::RP_PATH, Constants::Lidar::RPLIDAR_S1_BAUDRATE);
std::thread lidar_thread;
robot::types::DataPoint<points_t> last_points = {};
std::mutex points_lock;

namespace lidar {

void readLidarLoop() {
    std::vector<Polar2D> polarPts;    
    while (true) {
        auto scan = rp_lidar.poll();
        if (auto scan = rp_lidar.poll()) {
            std::vector<point_t> pts;
            double dtheta = (scan.value().angle_max-scan.value().angle_min)/(scan.value().ranges.size()-1);
            for (unsigned long i = 0; i < scan.value().ranges.size(); i++) {
                double rad = dtheta*i;
                double dist_m = scan.value().ranges[i];

                Polar2D frame{dist_m, rad};
                pts.push_back(lidar::polarToCartesian2(frame));
            }
        
            points_lock.lock();
            last_points = {pts};
            points_lock.unlock();
        } else {
            perror("failed to get frame");
            continue;
        }
    } 
}

/**
 * @brief Startsup RPLidar with default settings
 */
bool initializeLidar(double max_dist) {
    std::lock_guard<std::mutex> lk(points_lock);
    if (!lidar_initialized) {
        if (!rp_lidar.checkHealth()) {
            perror("failed to connect to rp lidar");
        } else {
            rp_lidar.setMaxDistance(max_dist);
            lidar_thread = std::thread(&readLidarLoop);
            lidar_initialized = true;
        }
    }
    return lidar_initialized;
}

/**
 * @brief Provides RP Lidar data at current timeframe
 */
robot::types::DataPoint<points_t> readLidar() {
    std::lock_guard<std::mutex> lk(points_lock);
    if (lidar_initialized) {
        robot::types::DataPoint<points_t> pts;
        pts = last_points;
        return pts;
    }
    return {};
}


} // namespace lidar
