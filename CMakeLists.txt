cmake_minimum_required(VERSION 2.8.3)
project(mpc_path_tracker)

find_package(cmake_modules REQUIRED)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
find_package(Eigen REQUIRED)

find_package(catkin REQUIRED COMPONENTS
  std_msgs
  geometry_msgs
  nav_msgs
  visualization_msgs
  pluginlib
  roscpp
  tf2_ros
  tf
  plan2control_msgs
)

include_directories(include ${catkin_INCLUDE_DIRS} ${Eigen_INCLUDE_DIRS})

catkin_package(
  INCLUDE_DIRS include
  LIBRARIES ${PROJECT_NAME}
  CATKIN_DEPENDS
  DEPENDS Eigen
)

add_executable(${PROJECT_NAME}_node src/mpc_path_tracker_node.cpp src/mpc_path_tracker.cpp)
target_link_libraries(${PROJECT_NAME}_node ${catkin_LIBRARIES} ${Eigen_LIBRARIES} ipopt)
