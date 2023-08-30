cmake_minimum_required(VERSION 3.0.2)
project(plan2control_msgs)

find_package(catkin REQUIRED COMPONENTS
  nav_msgs
  roscpp
  rospy
  std_msgs
  message_generation
)

add_message_files(
  FILES
  PathSpeed.msg
)

generate_messages(
  DEPENDENCIES
  std_msgs
  nav_msgs
)

catkin_package(
  CATKIN_DEPENDS
  nav_msgs
  roscpp
  rospy
  std_msgs
  message_runtime
)

include_directories(
  include
  ${catkin_INCLUDE_DIRS}
)


