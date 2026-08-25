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

// The whitespace set trim() strips and the trailing-comment rule below keys
// off of. '\v' and '\f' are rare in hand-edited config files, but a stray one
// used to leave trim() reporting a "line" that looked empty as instead
// malformed ("Malformed cvar line ''"), which reads like a complaint about
// nothing.
bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f';
}

std::string_view trim(std::string_view text) {
    usize begin = 0;
    while (begin < text.size() && is_space(text[begin])) {
        ++begin;
    }
    usize end = text.size();
    while (end > begin && is_space(text[end - 1])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

// A '#' or a leading "//" starts a *line* comment — the whole line is
// ignored. See strip_trailing_comment() below for the separate, narrower rule
// that lets a comment trail after a value on an otherwise real line.
bool is_comment(std::string_view line) {
    return line.starts_with("#") || line.starts_with("//");
}

// A '#' starts a *trailing* comment only when it is preceded by whitespace,
// so "window.title Room#3" keeps the '#' (part of the value) while
// "window.mode borderless   # my note" does not (comment, stripped). This
// runs for every cvar type, not just String, so "r.vsync 0 # off for now"
// also parses cleanly.
//
// Deliberately NOT "//": unlike a line that starts with "//" (a full-line
// comment, see is_comment()), "//" appearing after real content stays part
// of the value. Treating a trailing "//" as a comment marker would corrupt
// legitimate values that contain it, such as UNC paths ("//server/share")
// and URLs ("http://host/x"). Only '#' gets the trailing-comment treatment.
//
// A value that collapses to nothing but a comment returns empty — including
// when the "whitespace before #" was the key/value separator itself, e.g.
// "window.mode  # note" has no value at all once the comment is gone, and
// that must fail the same way an explicitly empty value does, not silently
// store "# note".
std::string_view strip_trailing_comment(std::string_view value) {
    if (value.starts_with('#')) {
        return {};
    }
    for (usize i = 1; i < value.size(); ++i) {
        if (value[i] == '#' && is_space(value[i - 1])) {
            return trim(value.substr(0, i));
        }
    }
    return value;
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
        if (rest.starts_with("=")) {
            return false;  // "key==value": a stray leftover separator, not a value
        }
    }
    value = trim(strip_trailing_comment(rest));
    return !key.empty() && !value.empty();
}

// Line number is 1-based when the value came from a config file. A value
// from the command line has no line, so callers pass 0, which means "omit
// the '(line N)' suffix" rather than "report line zero".
void apply_one(std::string_view key, std::string_view value, CvarSource source,
    usize line_number, CvarApplyStats& stats) {
    Cvar* cvar = find_cvar(key);
    if (!cvar) {
        ++stats.unknown;
        std::string message = std::string("Unknown cvar '") + std::string(key) + "'";
        if (line_number != 0) {
            message += " (line " + std::to_string(line_number) + ")";
        }
        log(LogLevel::Warn, LogChannel::General, message);
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
        {
            std::string message = std::string("Cvar '") + cvar->name() + "' rejected value '"
                + std::string(value) + "' (expected " + cvar_type_name(cvar->type());
            if (line_number != 0) {
                message += ", line " + std::to_string(line_number) + ")";
            } else {
                message += ")";
            }
            log(LogLevel::Warn, LogChannel::General, message);
        }
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

    // A UTF-8 BOM is invisible in most editors but glues itself to the first
    // key, turning a real cvar into an "Unknown cvar" report. Notepad's
    // "UTF-8 with BOM" and PowerShell 5.1's `Out-File -Encoding utf8` both
    // write one, so this is not a hypothetical. Strip it silently.
    //
    // A UTF-16 BOM means the whole file is the wrong encoding: read
    // byte-by-byte, every line degrades into one garbled "Unknown cvar"
    // warning. Refuse with a single clear message instead of forty bad ones.
    if (text.size() >= 3 && static_cast<u8>(text[0]) == 0xEF
        && static_cast<u8>(text[1]) == 0xBB && static_cast<u8>(text[2]) == 0xBF) {
        text.remove_prefix(3);
    } else if (text.size() >= 2
        && ((static_cast<u8>(text[0]) == 0xFF && static_cast<u8>(text[1]) == 0xFE)
            || (static_cast<u8>(text[0]) == 0xFE && static_cast<u8>(text[1]) == 0xFF))) {
        log(LogLevel::Warn, LogChannel::General,
            "Cvar config text looks like UTF-16 (found a UTF-16 byte-order mark); "
            "save the file as UTF-8 and try again");
        return stats;
    }

    usize begin = 0;
    usize line_number = 0;
    for (;;) {
        ++line_number;
        const usize newline = text.find('\n', begin);
        const usize end = newline == std::string_view::npos ? text.size() : newline;
        const std::string_view line = trim(text.substr(begin, end - begin));
        if (!line.empty() && !is_comment(line)) {
            std::string_view key;
            std::string_view value;
            if (split_line(line, key, value)) {
                apply_one(key, value, source, line_number, stats);
            } else {
                ++stats.invalid;
                log(LogLevel::Warn, LogChannel::General,
                    std::string("Malformed cvar line '") + std::string(line) + "' (line "
                        + std::to_string(line_number) + ")");
            }
        }
        if (newline == std::string_view::npos) {
            break;
        }
        begin = newline + 1;
    }
    return stats;
}

// split_line is deliberately not reused here: it applies strip_trailing_comment,
// which is a config-file rule ('#' preceded by whitespace starts a comment).
// A command line has no comments — a shell-quoted value like
// `--set window.title "Room # 3"` must keep its '#' verbatim. An empty value
// after '=' reaches Cvar::set, which rejects it as Invalid, which is the
// behaviour we want, so no extra empty-value check is needed here either.
CvarApplyStats apply_cvar_args(int argc, const char* const* argv) {
    CvarApplyStats stats{};
    if (argc <= 0 || !argv) {
        return stats;
    }
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i] ? argv[i] : "";
        if (arg != "--set") {
            continue;  // not an argument parser; --gates and friends are not ours
        }
        if (i + 1 >= argc) {
            ++stats.invalid;
            log(LogLevel::Warn, LogChannel::General, "--set with no key=value");
            break;
        }
        const std::string_view token = argv[++i] ? argv[i] : "";
        const usize equals = token.find('=');
        if (equals != std::string_view::npos) {
            apply_one(trim(token.substr(0, equals)), trim(token.substr(equals + 1)),
                CvarSource::CommandLine, 0, stats);
            continue;
        }
        if (i + 1 >= argc
            || std::string_view(argv[i + 1] ? argv[i + 1] : "") == "--set") {
            ++stats.invalid;
            log(LogLevel::Warn, LogChannel::General,
                std::string("--set ") + std::string(token) + " has no value");
            continue;
        }
        apply_one(token, trim(argv[++i] ? argv[i] : ""), CvarSource::CommandLine, 0, stats);
    }
    return stats;
}

} // namespace engine
