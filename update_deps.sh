#!/bin/bash

sudo apt update
sudo apt install -y build-essential unzip gnupg2 lsb-release git \
	cmake libeigen3-dev libopencv-dev libopencv-contrib-dev \
	libwebsocketpp-dev libboost-system-dev gpsd gpsd-clients libgps-dev nlohmann-json3-dev \
	catch2 urg-lidar rplidar ublox-linux hindsight-can=1.3.2 frozen libargparse-dev libavutil-dev \
	libx264-dev