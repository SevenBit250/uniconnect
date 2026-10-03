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

# Windows按PATH搜dll: 注入构建目录
set_tests_properties(abi dist_headers PROPERTIES
    ENVIRONMENT "PATH=${CMAKE_BINARY_DIR};$ENV{PATH}")
