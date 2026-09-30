#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

namespace ants::app {

/**
 * @brief The settings that the program remembers between runs: the original's "profile" (Ants.exe FUN_0100c18f / FUN_0100c20c / FUN_0100c27f / FUN_0100c2f8,
 * which keep the values in the registry under the keys of the string table: "Sound Volume", "Music Volume", "Scroll Speed", "Participate In Chat",
 * "Show Quick Help at Startup", "Quick Chat F9" ... "Quick Chat F12"; docs 5.51).
 *
 * The remake keeps the same names in a plain text file (`name=value`, one per line) in the user's application folder, or in the browser's local storage in
 * the web build. A store without a location (the default, and the tests') lives in memory only. Reads are the original's: a number is accepted when it is
 * a non-negative integer `min <= v < max` (the registry's DWORD test, FUN_0102950f), otherwise the default applies; a text is cut to its maximum and
 * cleared of anything but printable ASCII. Every write is saved at once, as the original's callbacks write the registry.
 */
class ConfigStore {
public:
    ConfigStore() = default;

    /// Where the settings are kept: a file path (native) or the local storage key (web). Empty: memory only.
    void set_location(std::string location) { location_ = std::move(location); }
    const std::string& location() const noexcept { return location_; }
    /// The platform's usual place: `settings.ini` in the per-user application folder, or the web storage key. Empty when there is none.
    static std::string default_location();

    /// Reads the stored settings (a missing file is an empty store). False when the file exists and cannot be read.
    bool load();
    /// Writes them. False on a write error; nothing is written for a store without a location.
    bool save() const;

    /// FUN_0102950f: the stored integer when it is a non-negative number in [min, max), else `def`
    int32_t get_int(const std::string& key, int32_t def, uint32_t min, uint32_t max) const;
    void set_int(const std::string& key, int32_t value);
    /// The stored text (printable ASCII only, at most `max_chars` characters), else `def`. A stored empty text is a value.
    std::string get_string(const std::string& key, const std::string& def, size_t max_chars) const;
    void set_string(const std::string& key, const std::string& value);

    bool has(const std::string& key) const { return values_.find(key) != values_.end(); }
    const std::map<std::string, std::string>& values() const noexcept { return values_; }

    /// The file's text (for the tests and the web store)
    std::string serialise() const;
    void parse(const std::string& text);

private:
    std::string location_;
    std::map<std::string, std::string> values_;
};

}  // namespace ants::app
