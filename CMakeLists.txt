cmake_minimum_required(VERSION 3.16)
project(PaperclipCounter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 如果你不是用 CMake 打开工程，而是在 VS 里手动配置，只需要：
#   附加包含目录 -> <OpenCV>\build\include
#   附加库目录   -> <OpenCV>\build\x64\vc16\lib
#   附加依赖项   -> opencv_world4100.lib （Debug 用 opencv_world4100d.lib）

find_package(OpenCV REQUIRED)

add_executable(paperclip_counter src/main.cpp)

target_include_directories(paperclip_counter PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(paperclip_counter PRIVATE ${OpenCV_LIBS})

# 源码里带中文注释，必须让 MSVC 按 UTF-8 解析，否则在中文系统(936 代码页)下会报 C4819 / 语法错误
if(MSVC)
    target_compile_options(paperclip_counter PRIVATE /utf-8)
endif()

# 把测试图片复制到输出目录，双击 exe 也能直接跑
add_custom_command(TARGET paperclip_counter POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:paperclip_counter>/images"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/images"
            "$<TARGET_FILE_DIR:paperclip_counter>/images"
    COMMENT "Copying sample images")

# 把 OpenCV 的 DLL 复制到 exe 旁边，省得每次配置 PATH
if(WIN32 AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)
    add_custom_command(TARGET paperclip_counter POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_RUNTIME_DLLS:paperclip_counter>"
                "$<TARGET_FILE_DIR:paperclip_counter>"
        COMMAND_EXPAND_LISTS
        COMMENT "Copying OpenCV runtime DLLs")
endif()
