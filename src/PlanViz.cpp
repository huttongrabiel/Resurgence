#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/point.hpp>

#include "simulator/graphics.h"
#include "simulator/utils.h"

using std::placeholders::_1;

class PlanViz : public rclcpp::Node
{
public:
	PlanViz()
		: Node("plan_visualization"),
			lidar_scan({}),
			viz_window("Planning visualization"),
			plan_sub(this->create_subscription<geometry_msgs::msg::Point>("plan_viz", 100, std::bind(&PlanViz::plan_callback, this, _1))),
			curr_pose_sub(this->create_subscription<geometry_msgs::msg::Point>("current_pose", 100, std::bind(&PlanViz::curr_pose_callback, this, _1))),
			next_pose_sub(this->create_subscription<geometry_msgs::msg::Point>("next_pose", 100, std::bind(&PlanViz::next_pose_callback, this, _1))),
			lidar_sub(this->create_subscription<geometry_msgs::msg::Point>("lidar_scan", 100, std::bind(&PlanViz::lidar_callback, this, _1)))
	{
		viz_window.display();
	}

private:
	void curr_pose_callback(const geometry_msgs::msg::Point::SharedPtr message)
	{
		viz_window.drawRobot(toTransform({message->x, message->y, message->z}), sf::Color::Black);
	}

	void next_pose_callback(const geometry_msgs::msg::Point::SharedPtr message)
	{
		viz_window.drawRobot(toTransform({message->x, message->y, message->z}), sf::Color::Blue);
	}

	void lidar_callback(const geometry_msgs::msg::Point::SharedPtr message)
	{
		lidar_scan.push_back({message->x, message->y, message->z});
	}

	void plan_callback(const geometry_msgs::msg::Point::SharedPtr message)
	{
		if (std::isnan(message->x) && std::isnan(message->y) && std::isnan(message->z))
		{
			// All data has been received, we can draw the visualization
			while (viz_window.pollWindowEvent() != -1) {}
			viz_window.drawPoints(lidar_scan, sf::Color::Red, 3);
			lidar_scan.clear();
			viz_window.display();
		}
		else
		{
			viz_window.drawRobot(toTransform({message->x, message->y, message->z}), sf::Color::Red);
		}
	}

	points_t lidar_scan;
	MyWindow viz_window;
	rclcpp::Subscription<geometry_msgs::msg::Point>::SharedPtr plan_sub;
	rclcpp::Subscription<geometry_msgs::msg::Point>::SharedPtr curr_pose_sub;
	rclcpp::Subscription<geometry_msgs::msg::Point>::SharedPtr next_pose_sub;
	rclcpp::Subscription<geometry_msgs::msg::Point>::SharedPtr lidar_sub;
};

int main(int argc, char **argv)
{
	// Initialize ROS without any commandline arguments
	rclcpp::init(0, nullptr);

	// Start reading data
	rclcpp::spin(std::make_shared<PlanViz>());

	// Shutdown ROS when done reading data
	rclcpp::shutdown();
	return 0;
}