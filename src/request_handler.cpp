#include "request_handler.h"

#include <boost/algorithm/string.hpp>

namespace http_handler {

std::string UrlDecode(std::string_view url) {
    std::string res;
    for (std::size_t i = 0; i < url.size(); ++i) {
        if (url[i] == '%' && (i + 2) < url.size()) {
            std::string hex = {url[i + 1], url[i + 2]};
            char dec = static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
            res.push_back(dec);
            i += 2;
        }
        else if (url[i] == '+') {
            res.push_back(' ');
        }
        else {
            res.push_back(url[i]);
        }
    }
    return res;
}

bool IsSubPath(const fs::path& path, const fs::path& base) {
    return path.native().starts_with(base.native());
}

std::string GetFileExtension(const fs::path& path) {
    return path.string().substr(path.string().rfind('.'));
}

std::string_view GetContentType(std::string ext) {
    boost::algorithm::to_lower(ext);
    if (ext == ".htm" || ext == ".html")
        return ContentType::TEXT_HTML;
    if (ext == ".css")
        return ContentType::CSS;
    if (ext == ".txt")
        return ContentType::TEXT_PLAIN;
    if (ext == ".js")
        return ContentType::JAVASCRIPT;
    if (ext == ".json")
        return ContentType::JSON;
    if (ext == ".xml")
        return ContentType::XML;
    if (ext == ".png")
        return ContentType::PNG;
    if (ext == ".jpg" || ext == ".jpe" || ext == ".jpeg")
        return ContentType::JPG;
    if (ext == ".gif")
        return ContentType::GIF;
    if (ext == ".bmp")
        return ContentType::BMP;
    if (ext == ".ico")
        return ContentType::ICO;
    if (ext == ".tiff" || ext == ".tif")
        return ContentType::TIFF;
    if (ext == ".svg" || ext == ".svgz")
        return ContentType::SVG_XML;
    if (ext == ".mp3")
        return ContentType::MP3;
    return ContentType::OCTET_STREAM;
}

}  // namespace http_handler
