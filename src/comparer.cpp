#include "comparer.h"
#include <fstream>
#include <string>

static std::string normalize(const std::string& s) {
    std::string result = s;

    while (!result.empty() &&
           (result.back() == '\n' ||
            result.back() == '\r' ||
            result.back() == ' ' ||
            result.back() == '\t')) {
        result.pop_back();
    }

    return result;
}

bool compare_output(
    const std::string& user_output_file,
    const std::string& standard_output_file
) {
    std::ifstream user_file(user_output_file);
    std::ifstream std_file(standard_output_file);

    if (!user_file.is_open() || !std_file.is_open()) {
        return false;
    }

    std::string user_content;
    std::string std_content;

    std::string line;

    while (std::getline(user_file, line)) {
        user_content += line + "\n";
    }

    while (std::getline(std_file, line)) {
        std_content += line + "\n";
    }

    return normalize(user_content) == normalize(std_content);
}