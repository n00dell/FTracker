// FTracker/core/src/paths.cpp
#include <pesa/pesa.h>

#include <cstdlib>

namespace pesa {
    namespace {
        std::filesystem::path envPath(const char *name) {
            const char *v = std::getenv(name);
            return (v && *v) ? std::filesystem::path(v) : std::filesystem::path{};
        }
    } // namespace

    std::filesystem::path defaultDatabasePath() {
        std::filesystem::path base;

#if defined(_WIN32)
        base = envPath("APPDATA"); // C:\Users\you\AppData\Roaming
#elif defined(__APPLE__)
        if (auto home = envPath("HOME"); !home.empty())
            base = home / "Library" / "Application Support";
#else
        base = envPath("XDG_DATA_HOME"); // Linux standard
        if (base.empty()) {
            if (auto home = envPath("HOME"); !home.empty()) base = home / ".local" / "share";
        }
#endif

        if (base.empty()) throw Error("Could not find a data folder. Use --db <path>.");
        return base / "pesa" / "pesa.db";
    }
} // namespace pesa
