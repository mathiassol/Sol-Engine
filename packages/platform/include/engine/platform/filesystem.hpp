#pragma once

#include <engine/core/types.hpp>

#include <span>
#include <string>
#include <vector>

namespace engine::platform {

class IFileSystem {
public:
    virtual ~IFileSystem() = default;

    virtual bool read_file(std::string_view path, std::vector<u8>& out) = 0;
    virtual bool write_file(std::string_view path, std::span<const u8> data) = 0;
    virtual bool exists(std::string_view path) const = 0;
    virtual std::string resolve(std::string_view path) const = 0;
};

} // namespace engine::platform
