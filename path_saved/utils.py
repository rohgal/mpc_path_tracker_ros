import os
import json

from rospy_message_converter import message_converter
from nav_msgs.msg import Path


def load_pathmsgs_from_jsons(folder_path):
    path_msgs = []
    index = 1

    while True:
        json_file_name = f"path{index}.json"
        json_file_path = os.path.join(folder_path, json_file_name)

        if not os.path.exists(json_file_path):
            break

        with open(json_file_path, "r") as json_file:
            json_data = json.load(json_file)

        path_msg = message_converter.convert_dictionary_to_ros_message('nav_msgs/Path', json_data)
        path_msgs.append(path_msg)

        index += 1

    return path_msgs