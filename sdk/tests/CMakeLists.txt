# ABI往返测试(源树头)
add_executable(test_abi test_abi.c)
target_include_directories(test_abi PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common)
target_link_libraries(test_abi PRIVATE uniconnect_upgrade uniconnect_can)
add_test(NAME abi COMMAND test_abi)

# 交付头自包含测试: 只给聚合目录(不给common/, 不给sdk/include), 链两库验证dllimport面
add_executable(test_dist_headers test_dist_headers.c)
target_include_directories(test_dist_headers PRIVATE ${CMAKE_BINARY_DIR}/dist-include)
target_link_libraries(test_dist_headers PRIVATE uniconnect_upgrade uniconnect_can)
add_test(NAME dist_headers COMMAND test_dist_headers)

# 真机CAN测试(需设备在线, 手工跑, 不进CTest默认集):
#   build/sdk-host/tests/can_hw_test.exe [每通道帧数]
add_executable(can_hw_test can_hw_test.c)
target_include_directories(can_hw_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common)
target_link_libraries(can_hw_test PRIVATE uniconnect_can)

# 真机升级测试(需设备在线APP态, 手工跑): 完整升级+50%中断注入+恢复+升级后CAN可用
#   build/sdk-host/tests/fwu_hw_test.exe <镜像.bin>   (通常 build/uniconnect.bin)
add_executable(fwu_hw_test fwu_hw_test.c)
target_include_directories(fwu_hw_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common)
target_link_libraries(fwu_hw_test PRIVATE uniconnect_upgrade uniconnect_can)

# Windows按PATH搜dll: 注入构建目录
set_tests_properties(abi dist_headers PROPERTIES
    ENVIRONMENT "PATH=${CMAKE_BINARY_DIR};$ENV{PATH}")
