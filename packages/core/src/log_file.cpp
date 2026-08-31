#include <engine/core/log_file.hpp>

#include <filesystem>

namespace engine {

std::string default_log_directory(std::string_view executable_directory) {
    return (std::filesystem::path(executable_directory) / "logs").string();
}

std::unique_ptr<ILogger> create_file_logger(std::string_view) {
    return nullptr;
}

bool install_file_logger(std::string_view) {
    return false;
}

} // namespace engine
