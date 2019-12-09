cmake_minimum_required(VERSION 2.8.3)
project(image_converter)

find_package(catkin REQUIRED
	COMPONENTS
	roscpp
	cv_bridge
	image_transport
	sensor_msgs
)

find_package( Boost REQUIRED )

catkin_package(
#  INCLUDE_DIRS include
#  LIBRARIES image_converter
  CATKIN_DEPENDS roscpp cv_bridge image_transport sensor_msgs
)

include_directories(
# include
${catkin_INCLUDE_DIRS}
)

add_executable( image_converter src/image_converter.cpp )
target_link_libraries( image_converter ${catkin_LIBRARIES} ${Boost_LIBRARIES})


