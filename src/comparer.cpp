#include "comparer.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string normalize_trailing_whitespace(const std::string& s) {
    std::string result = s;

    while (!result.empty()) {
        char ch = result.back();

        if (ch == '\n' || ch == '\r' || ch == ' ' || ch == '\t') {
            result.pop_back();
        } else {
            break;
        }
    }

    return result;
}

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool parse_double_full(const std::string& token, double& value) {
    if (token.empty()) {
        return false;
    }

    errno = 0;

    char* end = nullptr;
    value = std::strtod(token.c_str(), &end);

    if (end == token.c_str()) {
        return false;
    }

    if (*end != '\0') {
        return false;
    }

    if (errno == ERANGE) {
        return false;
    }

    if (!std::isfinite(value)) {
        return false;
    }

    return true;
}

bool almost_equal(
    double user_value,
    double standard_value,
    double abs_eps,
    double rel_eps
) {
    double diff = std::fabs(user_value - standard_value);
    double scale = std::max(
        1.0,
        std::max(std::fabs(user_value), std::fabs(standard_value))
    );

    double allowed = std::max(abs_eps, rel_eps * scale);

    return diff <= allowed;
}

} // namespace

bool is_valid_compare_mode(const std::string& mode) {
    std::string normalized = to_lower(mode);
    return normalized == "exact" ||
           normalized == "floating" ||
           normalized == "float";
}

CompareMode compare_mode_from_string(const std::string& mode) {
    std::string normalized = to_lower(mode);

    if (normalized == "floating" || normalized == "float") {
        return CompareMode::FLOATING;
    }

    return CompareMode::EXACT;
}

std::string compare_mode_to_string(CompareMode mode) {
    switch (mode) {
        case CompareMode::FLOATING:
            return "floating";
        case CompareMode::EXACT:
        default:
            return "exact";
    }
}

bool compare_output(
    const std::string& user_output_file,
    const std::string& standard_output_file
) {
    return compare_output_exact(user_output_file, standard_output_file);
}

bool compare_output(
    const std::string& user_output_file,
    const std::string& standard_output_file,
    CompareMode mode,
    double abs_eps,
    double rel_eps
) {
    switch (mode) {
        case CompareMode::FLOATING:
            return compare_output_floating(
                user_output_file,
                standard_output_file,
                abs_eps,
                rel_eps
            );
        case CompareMode::EXACT:
        default:
            return compare_output_exact(user_output_file, standard_output_file);
    }
}

bool compare_output_exact(
    const std::string& user_output_file,
    const std::string& standard_output_file
) {
    std::ifstream user_file(user_output_file);
    std::ifstream standard_file(standard_output_file);

    if (!user_file.is_open() || !standard_file.is_open()) {
        return false;
    }

    std::ostringstream user_buffer;
    std::ostringstream standard_buffer;

    user_buffer << user_file.rdbuf();
    standard_buffer << standard_file.rdbuf();

    return normalize_trailing_whitespace(user_buffer.str()) ==
           normalize_trailing_whitespace(standard_buffer.str());
}

bool compare_output_floating(
    const std::string& user_output_file,
    const std::string& standard_output_file,
    double abs_eps,
    double rel_eps
) {
    std::ifstream user_file(user_output_file);
    std::ifstream standard_file(standard_output_file);

    if (!user_file.is_open() || !standard_file.is_open()) {
        return false;
    }

    std::string user_token;
    std::string standard_token;

    while (true) {
        bool has_user_token = static_cast<bool>(user_file >> user_token);
        bool has_standard_token = static_cast<bool>(standard_file >> standard_token);

        if (has_user_token != has_standard_token) {
            return false;
        }

        if (!has_user_token) {
            return true;
        }

        double user_value = 0.0;
        double standard_value = 0.0;

        bool user_is_number = parse_double_full(user_token, user_value);
        bool standard_is_number = parse_double_full(standard_token, standard_value);

        if (user_is_number && standard_is_number) {
            if (!almost_equal(user_value, standard_value, abs_eps, rel_eps)) {
                return false;
            }
        } else {
            if (user_token != standard_token) {
                return false;
            }
        }
    }
}
