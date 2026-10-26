#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

#include "MediaCover.h"

#include "WallpaperEngine/Assets/AssetLoadException.h"
#include "WallpaperEngine/Logging/Log.h"
#include "WallpaperEngine/Media/MediaSource.h"

using namespace WallpaperEngine::FileSystem;
using namespace WallpaperEngine::FileSystem::Adapters;

namespace {
/**
 * Decodes %XX escapes used in file:// URLs
 */
std::string percentDecode (const std::string& input) {
    std::string result;
    result.reserve (input.size ());

    for (size_t i = 0; i < input.size (); i++) {
	if (input[i] == '%' && i + 2 < input.size () && std::isxdigit (input[i + 1]) && std::isxdigit (input[i + 2])) {
	    result.push_back (static_cast<char> (std::stoi (input.substr (i + 1, 2), nullptr, 16)));
	    i += 2;
	} else {
	    result.push_back (input[i]);
	}
    }

    return result;
}

std::string base64Decode (const std::string& input) {
    static constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve (input.size () * 3 / 4);
    uint32_t buffer = 0;
    int bits = 0;

    for (const char c : input) {
	const auto index = alphabet.find (c);

	if (index == std::string_view::npos) {
	    // padding, whitespace and invalid characters are skipped
	    continue;
	}

	buffer = (buffer << 6) | static_cast<uint32_t> (index);
	bits += 6;

	if (bits >= 8) {
	    bits -= 8;
	    result.push_back (static_cast<char> ((buffer >> bits) & 0xFF));
	}
    }

    return result;
}
} // namespace

ReadStreamSharedPtr MediaCoverAdapter::open (const std::filesystem::path& path) const {
    if (path != "$mediaThumbnail") {
	throw std::filesystem::filesystem_error (
	    "MediaCoverAdapter only supports $mediaThumbnail", path, std::error_code ()
	);
    }

    if (!source.getMediaInfo ().url.has_value ()) {
	throw std::filesystem::filesystem_error ("Media source does not have a valid URL", path, std::error_code ());
    }

    std::string album = *source.getMediaInfo ().url;

    // some players (like mpv-mpris) embed the cover as a base64 data URI
    if (album.starts_with ("data:")) {
	const auto separator = album.find (";base64,");

	if (separator == std::string::npos) {
	    throw std::filesystem::filesystem_error (
		"Only base64 data URIs are supported for media covers", path, std::error_code ()
	    );
	}

	return std::make_shared<std::istringstream> (base64Decode (album.substr (separator + 8)));
    }

    if (album.starts_with ("file://")) {
	album = percentDecode (album.substr (7));
    } else {
	throw std::filesystem::filesystem_error (
	    "Only file:// and data: URLs are supported for media covers", album, std::error_code ()
	);
    }

    std::filesystem::path file = std::filesystem::absolute (album);

    if (std::filesystem::exists (file) == false) {
	throw std::filesystem::filesystem_error ("Media file does not exist", file, std::error_code ());
    }

    if (std::filesystem::is_regular_file (file) == false) {
	throw std::filesystem::filesystem_error ("Media file is not a regular file", file, std::error_code ());
    }

    return std::make_shared<std::ifstream> (file);
}

bool MediaCoverAdapter::exists (const std::filesystem::path& path) const { return path == ""; }

std::filesystem::path MediaCoverAdapter::physicalPath (const std::filesystem::path& path) const {
    sLog.exception ("MediaCoverAdapter does not support realpath");
}

bool MediaCoverFactory::handlesMountpoint (const std::filesystem::path& path) const {
    return path == "$mediaThumbnail";
}

AdapterSharedPtr MediaCoverFactory::create (const std::filesystem::path& path) const {
    if (path != "$mediaThumbnail") {
	sLog.exception ("MediaCoveradapter only supports $mediaThumbnail");
    }

    return std::make_unique<MediaCoverAdapter> (source);
}