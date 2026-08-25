#pragma once

#include <engine/platform/filesystem.hpp>
#include <engine/platform/input.hpp>
#include <engine/platform/window.hpp>

#include <memory>
#include <string>
#include <string_view>

namespace engine::platform {

// Factory interface — each platform backend implements this.
// Swap backends by linking a different platform-* package.
class IPlatform {
public:
    virtual ~IPlatform() = default;

    virtual std::unique_ptr<IWindow> create_window(const WindowDesc& desc) = 0;
    virtual std::unique_ptr<IInput> create_input(IWindow& window) = 0;
    virtual std::unique_ptr<IFileSystem> create_filesystem() = 0;
    virtual std::string executable_directory() const = 0;
    virtual std::string executable_file_version() const = 0;
    virtual bool executable_has_icon() const = 0;

    virtual std::string_view name() const = 0;
};

} // namespace engine::platform
