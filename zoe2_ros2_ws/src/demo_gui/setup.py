from setuptools import find_packages, setup

package_name = 'demo_gui'

setup(
    name=package_name,
    version='0.0.0',
    packages=find_packages(exclude=['test']),
    data_files=[
        ('share/ament_index/resource_index/packages',
            ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
    ],
    install_requires=['setuptools'],
    zip_safe=True,
    maintainer='Aragya Goyal',
    maintainer_email='agoyal1642@gmail.com',
    description='Publishes information about battery and allows for visualization over foxglove.',
    license='Apache-2.0',
    extras_require={
        'test': [
            'pytest',
        ],
    },
    entry_points={
        'console_scripts': [
            'talker = demo_gui.publisher_member_function:main',
            'listener = demo_gui.subscriber_member_function:main'
        ],
    },
)
