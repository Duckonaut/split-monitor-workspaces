#include <sstream>
#include <string>
#include <vector>

namespace helpers {

/**
 * @brief Split a string by a delimiter.
 */
inline std::vector<std::string> splitString(const std::string& str, char delimiter)
{
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

/**
 * @brief Based on a given direction string, return an int representing the delta.
 */
inline int getDelta(const std::string& direction)
{
    if (direction == "next" || direction == "+1" || direction == "1") {
        return 1;
    }
    if (direction == "prev" || direction == "-1") {
        return -1;
    }
    // fallback if input is incorrect
    return 0;
}

} // namespace helpers