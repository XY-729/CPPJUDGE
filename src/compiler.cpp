#include "compiler.h"

#include <cstdlib>
#include <iostream>
#include <fstream>

bool compile_cpp(
    const std::string& source_file,
    const std::string& executable_file,
    const std::string& error_file
) {
    // 先清空旧的编译错误文件
    std::ofstream clear_file(error_file);
    clear_file.close();

    std::string command =
        "g++ " + source_file +
        " -o " + executable_file +
        " -std=c++17 -O2 " +
        "2> " + error_file;

    std::cout << "[Compile] " << command << std::endl;

    int result = system(command.c_str());

    return result == 0;
}