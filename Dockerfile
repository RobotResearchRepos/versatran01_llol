FROM osrf/ros:noetic-desktop-full

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
 && apt-get install -y git \
 && rm -rf /var/lib/apt/lists/*

# Source code dependencies
RUN git clone --depth 1 --branch v0.6.0 https://github.com/google/glog.git \
 && cd glog && cmake -S . -B build -G "Unix Makefiles" -DCMAKE_CXX_STANDARD=17 \
 && cmake --build build && cmake --build build --target install

RUN git clone --depth 1 --branch 8.1.0 https://github.com/fmtlib/fmt.git \
 && cd fmt && cmake -DCMAKE_POSITION_INDEPENDENT_CODE=TRUE -DCMAKE_CXX_STANDARD=17 -DFMT_TEST=False \
 && make install

RUN git clone --depth 1 --branch 20220623.0 https://github.com/abseil/abseil-cpp.git \
 && cd abseil-cpp && mkdir build && cd build \
 && cmake -DABSL_BUILD_TESTING=OFF -DCMAKE_CXX_STANDARD=17 -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_POSITION_INDEPENDENT_CODE=ON .. \
 && cmake --build . --target install

RUN git clone https://github.com/strasdat/Sophus.git \
 && cd Sophus && git checkout 785fef3 \
 && mkdir build && cd build && cmake -DBUILD_SOPHUS_TESTS=OFF -DBUILD_SOPHUS_EXAMPLES=OFF -DCMAKE_CXX_STANDARD=17 .. \
 && make install
 
# Code repository

RUN mkdir -p /catkin_ws/src/

RUN git clone --recurse-submodules \
      https://github.com/RobotResearchRepos/versatran01_llol \
      /catkin_ws/src/llol

RUN . /opt/ros/$ROS_DISTRO/setup.sh \
 && apt-get update \
 && rosdep install -r -y \
     --from-paths /catkin_ws/src \
     --ignore-src \
 && rm -rf /var/lib/apt/lists/*

RUN . /opt/ros/$ROS_DISTRO/setup.sh \
 && cd /catkin_ws \
 && catkin_make
 
 
