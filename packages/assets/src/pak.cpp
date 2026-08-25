#include <engine/assets/pak.hpp>

#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace engine::assets {
namespace {

constexpr u8 kMagic[4] = {'S', 'O', 'L', 'P'};
constexpr u32 kMaxEntries = 65535;
constexpr u32 kMaxNameLen = 512;

void append_u32_le(std::vector<u8>& out, u32 value) {
    out.push_back(static_cast<u8>(value));
    out.push_back(static_cast<u8>(value >> 8));
    out.push_back(static_cast<u8>(value >> 16));
    out.push_back(static_cast<u8>(value >> 24));
}

struct Cursor {
    std::span<const u8> bytes;
    usize pos = 0;

    bool remaining(usize n) const {
        return pos <= bytes.size() && n <= bytes.size() - pos;
    }

    bool read_u32(u32& value) {
        if (!remaining(4)) {
            return false;
        }
        value = static_cast<u32>(bytes[pos])
            | (static_cast<u32>(bytes[pos + 1]) << 8)
            | (static_cast<u32>(bytes[pos + 2]) << 16)
            | (static_cast<u32>(bytes[pos + 3]) << 24);
        pos += 4;
        return true;
    }

    bool read_bytes(usize n, std::string& out) {
        if (!remaining(n)) {
            return false;
        }
        out.assign(reinterpret_cast<const char*>(bytes.data() + pos), n);
        pos += n;
        return true;
    }
};

struct TocRecord {
    std::string name;
    u32 offset = 0;
    u32 size = 0;
};

bool valid_pak_name(std::string_view name) {
    if (name.empty() || name.size() > kMaxNameLen || name.front() != '/' || name.back() == '/') {
        return false;
    }
    usize i = 1;
    while (i < name.size()) {
        const usize slash = name.find('/', i);
        const std::string_view part = name.substr(i, slash == std::string_view::npos ? std::string_view::npos : slash - i);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        for (char c : part) {
            if (c == '\\' || c == '\0' || static_cast<unsigned char>(c) < 32) {
                return false;
            }
        }
        if (slash == std::string_view::npos) {
            break;
        }
        i = slash + 1;
    }
    return true;
}

bool parse_pak(std::span<const u8> bytes, std::vector<TocRecord>& toc) {
    toc.clear();
    if (bytes.size() < 12) {
        return false;
    }
    if (std::memcmp(bytes.data(), kMagic, 4) != 0) {
        return false;
    }
    Cursor cur{bytes, 4};
    u32 version = 0;
    u32 count = 0;
    if (!cur.read_u32(version) || !cur.read_u32(count)) {
        return false;
    }
    if (version != kPakVersion || count == 0 || count > kMaxEntries) {
        return false;
    }

    toc.reserve(count);
    std::unordered_set<std::string> seen;
    for (u32 i = 0; i < count; ++i) {
        u32 name_len = 0;
        if (!cur.read_u32(name_len) || name_len == 0 || name_len > kMaxNameLen) {
            return false;
        }
        TocRecord rec{};
        if (!cur.read_bytes(name_len, rec.name) || !cur.read_u32(rec.offset) || !cur.read_u32(rec.size)) {
            return false;
        }
        if (!valid_pak_name(rec.name) || rec.size == 0 || !seen.insert(rec.name).second) {
            return false;
        }
        toc.push_back(std::move(rec));
    }

    const u64 toc_end = static_cast<u64>(cur.pos);
    u64 expected = toc_end;
    for (const TocRecord& rec : toc) {
        if (static_cast<u64>(rec.offset) != expected) {
            return false;
        }
        expected += rec.size;
        if (expected > static_cast<u64>(bytes.size()) || expected < rec.size) {
            return false;
        }
    }
    return expected == static_cast<u64>(bytes.size());
}

} // namespace

bool peek_pak(std::span<const u8> bytes) {
    std::vector<TocRecord> toc;
    return parse_pak(bytes, toc);
}

bool write_pak(std::span<const PakEntry> entries, std::vector<u8>& out) {
    if (entries.empty() || entries.size() > kMaxEntries) {
        return false;
    }

    std::unordered_set<std::string> seen;
    u64 toc_bytes = 0;
    u64 payload_bytes = 0;
    for (const PakEntry& entry : entries) {
        if (!valid_pak_name(entry.name) || entry.bytes.empty()
            || entry.bytes.size() > 0xffffffffu || !seen.insert(entry.name).second) {
            return false;
        }
        toc_bytes += 12ull + static_cast<u64>(entry.name.size());
        payload_bytes += entry.bytes.size();
    }

    const u64 payload_base = 12ull + toc_bytes;
    const u64 total = payload_base + payload_bytes;
    if (payload_base > 0xffffffffull || total > 0xffffffffull || total < payload_base) {
        return false;
    }

    std::vector<u8> blob;
    blob.reserve(static_cast<usize>(total));
    blob.insert(blob.end(), kMagic, kMagic + 4);
    append_u32_le(blob, kPakVersion);
    append_u32_le(blob, static_cast<u32>(entries.size()));

    u32 offset = static_cast<u32>(payload_base);
    for (const PakEntry& entry : entries) {
        append_u32_le(blob, static_cast<u32>(entry.name.size()));
        blob.insert(blob.end(), entry.name.begin(), entry.name.end());
        append_u32_le(blob, offset);
        append_u32_le(blob, static_cast<u32>(entry.bytes.size()));
        offset += static_cast<u32>(entry.bytes.size());
    }
    for (const PakEntry& entry : entries) {
        blob.insert(blob.end(), entry.bytes.begin(), entry.bytes.end());
    }
    out = std::move(blob);
    return true;
}

bool read_pak_entry(std::span<const u8> bytes, std::string_view name, std::vector<u8>& out) {
    std::vector<TocRecord> toc;
    if (!parse_pak(bytes, toc)) {
        return false;
    }
    for (const TocRecord& rec : toc) {
        if (rec.name == name) {
            out.assign(bytes.begin() + rec.offset, bytes.begin() + rec.offset + rec.size);
            return true;
        }
    }
    return false;
}

namespace {

class PakAssetLoader final : public IAssetLoader {
public:
    explicit PakAssetLoader(std::vector<u8> blob, std::unordered_map<std::string, std::pair<u32, u32>> table)
        : blob_(std::move(blob)), table_(std::move(table)) {}

    bool mount(std::string_view name, std::string_view) override {
        while (!name.empty() && (name.front() == '/' || name.front() == '\\')) {
            name.remove_prefix(1);
        }
        return !name.empty();
    }

    bool resolve_path(std::string_view virtual_or_physical, std::string& out_physical) const override {
        if (table_.find(std::string(virtual_or_physical)) == table_.end()) {
            return false;
        }
        out_physical = std::string(virtual_or_physical);
        return true;
    }

    bool load_bytes(std::string_view path, std::vector<u8>& out) override {
        const auto it = table_.find(std::string(path));
        if (it == table_.end()) {
            return false;
        }
        const u32 offset = it->second.first;
        const u32 size = it->second.second;
        out.assign(blob_.begin() + offset, blob_.begin() + offset + size);
        return true;
    }

    void unload(AssetHandle) override {}

private:
    std::vector<u8> blob_;
    std::unordered_map<std::string, std::pair<u32, u32>> table_;
};

} // namespace

std::unique_ptr<IAssetLoader> create_pak_loader(std::span<const u8> bytes) {
    std::vector<TocRecord> toc;
    if (!parse_pak(bytes, toc)) {
        return nullptr;
    }
    std::unordered_map<std::string, std::pair<u32, u32>> table;
    table.reserve(toc.size());
    for (const TocRecord& rec : toc) {
        table.emplace(rec.name, std::pair<u32, u32>{rec.offset, rec.size});
    }
    return std::make_unique<PakAssetLoader>(
        std::vector<u8>(bytes.begin(), bytes.end()), std::move(table));
}

} // namespace engine::assets
