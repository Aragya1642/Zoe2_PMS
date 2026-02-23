import sys
if sys.prefix == '/usr':
    sys.real_prefix = sys.prefix
    sys.prefix = sys.exec_prefix = '/home/agoyal1642/Documents/Zoe2_PMS/zoe2_ros2_ws/install/demo_gui'
