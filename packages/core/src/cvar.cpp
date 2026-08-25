#include <engine/core/cvar.hpp>

#include <engine/core/assert.hpp>
#include <engine/core/log.hpp>

#include <charconv>
#include <cmath>
#include <vector>

namespace engine {

namespace {

// Function-local static: safe no matter what order translation units run their
// static constructors in.
std::vector<Cvar*>& registry() {
    static std::vector<Cvar*> list;
    return list;
}

bool ascii_iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (usize i = 0; i < a.size(); ++i) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') { ca = static_cast<char>(ca - 'A' + 'a'); }
        if (cb >= 'A' && cb <= 'Z') { cb = static_cast<char>(cb - 'A' + 'a'); }
        if (ca != cb) {
            return false;
        }
    }
    return true;
}

bool parse_bool(std::string_view text, bool& out) {
    if (ascii_iequals(text, "1") || ascii_iequals(text, "true")
        || ascii_iequals(text, "on") || ascii_iequals(text, "yes")) {
        out = true;
        return true;
    }
    if (ascii_iequals(text, "0") || ascii_iequals(text, "false")
        || ascii_iequals(text, "off") || ascii_iequals(text, "no")) {
        out = false;
        return true;
    }
    return false;
}

bool parse_int(std::string_view text, i32& out) {
    if (text.empty()) {
        return false;
    }
    // from_chars never consumes a leading '+'; skip at most one so "+5" parses
    // like "5". Only skip it when a digit immediately follows — otherwise
    // reject outright. Without that check, stripping "+" from "+-1" would
    // leave "-1", which from_chars happily parses as a negative number.
    if (text.front() == '+') {
        if (text.size() < 2 || text[1] < '0' || text[1] > '9') {
            return false;
        }
        text.remove_prefix(1);
    }
    i32 value = 0;
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc{} || result.ptr != last) {
        return false;  // trailing junk is a typo, not a number
    }
    out = value;
    return true;
}

bool parse_float(std::string_view text, f32& out) {
    if (text.empty()) {
        return false;
    }
    // Same leading-'+' handling as parse_int, but a digit or a decimal point
    // may legitimately follow (".5", "+.5").
    if (text.front() == '+') {
        if (text.size() < 2 || !((text[1] >= '0' && text[1] <= '9') || text[1] == '.')) {
            return false;
        }
        text.remove_prefix(1);
    }
    f32 value = 0.f;
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc{} || result.ptr != last) {
        return false;
    }
    if (!std::isfinite(value)) {
        return false;  // reject inf/nan spellings that from_chars accepts
    }
    out = value;
    return true;
}

std::string_view trim(std::string_view text) {
    usize begin = 0;
    while (begin < text.size()
        && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r')) {
        ++begin;
    }
    usize end = text.size();
    while (end > begin
        && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r')) {
        --end;
    }
    return text.substr(begin, end - begin);
}

bool is_comment(std::string_view line) {
    return line.starts_with("#") || line.starts_with("//");
}

// "key value", "key = value" and "key=value" all split the same way.
bool split_line(std::string_view line, std::string_view& key, std::string_view& value) {
    const usize split = line.find_first_of(" \t=");
    if (split == std::string_view::npos) {
        return false;  // a bare key with no value
    }
    key = trim(line.substr(0, split));
    std::string_view rest = trim(line.substr(split));
    if (rest.starts_with("=")) {
        rest = trim(rest.substr(1));
    }
    value = rest;
    return !key.empty() && !value.empty();
}

void apply_one(std::string_view key, std::string_view value, CvarSource source,
    CvarApplyStats& stats) {
    Cvar* cvar = find_cvar(key);
    if (!cvar) {
        ++stats.unknown;
        log(LogLevel::Warn, LogChannel::General,
            std::string("Unknown cvar '") + std::string(key) + "'");
        return;
    }
    switch (cvar->set(value, source)) {
    case CvarSetResult::Applied:
        ++stats.applied;
        break;
    case CvarSetResult::Ignored:
        ++stats.ignored;
        break;
    case CvarSetResult::Invalid:
        ++stats.invalid;
        log(LogLevel::Warn, LogChannel::General,
            std::string("Cvar '") + cvar->name() + "' rejected value '"
                + std::string(value) + "' (expected "
                + cvar_type_name(cvar->type()) + ")");
        break;
    }
}

void register_cvar(Cvar* cvar) {
    ENGINE_ASSERT(cvar != nullptr);
    for (Cvar* existing : registry()) {
        ENGINE_ASSERT_MSG(!ascii_iequals(existing->name(), cvar->name()),
            "duplicate cvar name");
    }
    registry().push_back(cvar);
}

} // namespace

const char* cvar_type_name(CvarType type) {
    switch (type) {
    case CvarType::Bool:   return "bool";
    case CvarType::Int:    return "int";
    case CvarType::Float:  return "float";
    case CvarType::String: return "string";
    }
    return "???";  // corrupted value: announce itself rather than impersonate bool
}

Cvar::Cvar(const char* name, bool value, const char* help)
    : name_(name), help_(help), type_(CvarType::Bool), bool_(value) {
    register_cvar(this);
}

Cvar::Cvar(const char* name, i32 value, const char* help)
    : name_(name), help_(help), type_(CvarType::Int), int_(value) {
    register_cvar(this);
}

Cvar::Cvar(const char* name, f32 value, const char* help)
    : name_(name), help_(help), type_(CvarType::Float), float_(value) {
    register_cvar(this);
}

Cvar::Cvar(const char* name, const char* value, const char* help)
    : name_(name), help_(help), type_(CvarType::String), string_(value ? value : "") {
    register_cvar(this);
}

Cvar::~Cvar() {
    std::erase(registry(), this);
}

bool Cvar::as_bool() const {
    ENGINE_ASSERT_MSG(type_ == CvarType::Bool, "cvar read as the wrong type");
    return bool_;
}

i32 Cvar::as_int() const {
    ENGINE_ASSERT_MSG(type_ == CvarType::Int, "cvar read as the wrong type");
    return int_;
}

f32 Cvar::as_float() const {
    ENGINE_ASSERT_MSG(type_ == CvarType::Float, "cvar read as the wrong type");
    return float_;
}

std::string_view Cvar::as_string() const {
    ENGINE_ASSERT_MSG(type_ == CvarType::String, "cvar read as the wrong type");
    return string_;
}

CvarSetResult Cvar::set(std::string_view text, CvarSource source) {
    // Parse before the precedence check, so a typo in a shadowed line is still
    // reported as a typo.
    bool bool_value = false;
    i32 int_value = 0;
    f32 float_value = 0.f;
    switch (type_) {
    case CvarType::Bool:
        if (!parse_bool(text, bool_value)) { return CvarSetResult::Invalid; }
        break;
    case CvarType::Int:
        if (!parse_int(text, int_value)) { return CvarSetResult::Invalid; }
        break;
    case CvarType::Float:
        if (!parse_float(text, float_value)) { return CvarSetResult::Invalid; }
        break;
    case CvarType::String:
        if (text.empty()) { return CvarSetResult::Invalid; }
        break;
    }

    if (static_cast<u8>(source) < static_cast<u8>(source_)) {
        return CvarSetResult::Ignored;
    }

    switch (type_) {
    case CvarType::Bool:   bool_ = bool_value; break;
    case CvarType::Int:    int_ = int_value; break;
    case CvarType::Float:  float_ = float_value; break;
    case CvarType::String: string_.assign(text); break;
    }
    source_ = source;
    return CvarSetResult::Applied;
}

Cvar* find_cvar(std::string_view name) {
    for (Cvar* cvar : registry()) {
        if (ascii_iequals(cvar->name(), name)) {
            return cvar;
        }
    }
    return nullptr;
}

usize cvar_count() {
    return registry().size();
}

Cvar* cvar_at(usize index) {
    return index < registry().size() ? registry()[index] : nullptr;
}

CvarApplyStats apply_cvar_text(std::string_view text, CvarSource source) {
    CvarApplyStats stats{};
    usize begin = 0;
    while (begin <= text.size()) {
        const usize newline = text.find('\n', begin);
        const usize end = newline == std::string_view::npos ? text.size() : newline;
        const std::string_view line = trim(text.substr(begin, end - begin));
        if (!line.empty() && !is_comment(line)) {
            std::string_view key;
            std::string_view value;
            if (split_line(line, key, value)) {
                apply_one(key, value, source, stats);
            } else {
                ++stats.invalid;
                log(LogLevel::Warn, LogChannel::General,
                    std::string("Malformed cvar line '") + std::string(line) + "'");
            }
        }
        if (newline == std::string_view::npos) {
            break;
        }
        begin = newline + 1;
    }
    return stats;
}

CvarApplyStats apply_cvar_args(int, const char* const*) {
    return {};
}

} // namespace engine
