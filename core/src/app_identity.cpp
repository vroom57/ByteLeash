#include "byteleash/app_identity.h"

#include <algorithm>

namespace byteleash {

namespace {

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

// Copies one folder name, replacing each version number with "*".
void appendSegment(std::string& out, std::string_view segment) {
    std::size_t i = 0;
    while (i < segment.size()) {
        if (!isDigit(segment[i])) {
            out += segment[i++];
            continue;
        }
        // Scan digits(.digits)+ greedily.
        std::size_t end = i;
        int groups = 0;
        while (true) {
            std::size_t digits_end = end;
            while (digits_end < segment.size() && isDigit(segment[digits_end])) {
                ++digits_end;
            }
            if (digits_end == end) {
                break;  // No digits after a dot: the dot isn't part of it.
            }
            end = digits_end;
            ++groups;
            if (end + 1 < segment.size() && segment[end] == '.' &&
                isDigit(segment[end + 1])) {
                ++end;
                continue;
            }
            break;
        }
        if (groups >= 2) {
            out += '*';
        } else {
            out.append(segment.substr(i, end - i));
        }
        i = end;
    }
}

}  // namespace

std::string wildcardVersionFolders(std::string_view path) {
    const std::size_t last_sep = path.find_last_of("\\/");
    if (last_sep == std::string_view::npos) {
        return std::string(path);
    }

    std::string out;
    out.reserve(path.size());
    std::size_t start = 0;
    while (start <= last_sep) {
        const std::size_t sep = path.find_first_of("\\/", start);
        appendSegment(out, path.substr(start, sep - start));
        out += path[sep];
        start = sep + 1;
    }
    out.append(path.substr(last_sep + 1));
    return out;
}

std::string serviceHostIdentity(std::string_view exe_name,
                                std::vector<std::string> services) {
    std::sort(services.begin(), services.end());
    std::string out(exe_name);
    out += " [";
    for (std::size_t i = 0; i < services.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += services[i];
    }
    out += ']';
    return out;
}

}  // namespace byteleash
