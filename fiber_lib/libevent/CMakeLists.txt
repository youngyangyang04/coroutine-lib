cmake_minimum_required(VERSION 3.10)
project(EchoServer)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 查找 libevent 库
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBEVENT REQUIRED libevent)

# 包含 libevent 和 libco 的头文件路径
include_directories(${LIBEVENT_INCLUDE_DIRS})
include_directories(/home/lhs/apache_corlib/libco/include) # 替换为 libco 的头文件路径

# 添加可执行文件
add_executable(echo_server ../main.cpp)

# 链接 libevent, libco, pthread 和 dl 库
target_link_libraries(echo_server 
    ${LIBEVENT_LIBRARIES} 
    /home/lhs/apache_corlib/libco/lib/libcolib.a  # 替换为 libco 静态库路径
    pthread 
    dl
)
