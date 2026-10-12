#pragma once

#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace cli {

inline const std::string kCommonFooter =
    "Copyright (c) 2025 Alpha Parallel Computing Research Group, University of Torino, Italy, "
    "and Parallel Programming Models Group, University of Pisa, Italy\n";

class Arg {
  public:
    std::string value;

    template <typename T> T get() const {
        T out{};
        std::istringstream(value) >> out;
        return out;
    }
};

class Parser {
  public:
    explicit Parser(const std::string &program_desc) : _intro(program_desc + "\nArguments:\n") {
        add('h', "help", "", "Display this help menu");
    }

    void add(const char short_opt, const std::string &long_opt, const std::string &default_value,
             const std::string &help) {
        _options[long_opt] = Arg{default_value};
        if (short_opt) {
            _keys[std::string(1, short_opt)] = long_opt;
        }

        const std::string flag_display = short_opt
                                             ? "  -" + std::string(1, short_opt) + ", --" + long_opt
                                             : "      --" + long_opt;
        _help_lines.push_back(flag_display + "  " + help +
                              (default_value.empty() ? "" : " (default: " + default_value + ")"));
    }

    bool parse(const int argc, char **argv) {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            std::string key;
            if (arg.rfind("--", 0) == 0) {
                key = arg.substr(2);
            } else if (arg.size() >= 2 && arg[0] == '-') {
                key = std::string(1, arg[1]);
            } else {
                continue; // positional argument, ignore
            }

            if (auto it = _keys.find(key); it != _keys.end()) {
                key = it->second;
            }

            if (key == "help") {
                _help = true;
                return false;
            }

            auto opt = _options.find(key);
            if (opt == _options.end() || i + 1 >= argc) {
                return false; // unknown option or missing value
            }
            opt->second.value = argv[++i];
        }
        return true;
    }

    Arg &operator[](const std::string &key) { return _options[key]; }

    [[nodiscard]] bool help_requested() const { return _help; }

    [[nodiscard]] std::string help() const {
        std::string out = _intro;
        for (const auto &line : _help_lines) {
            out += line + "\n";
        }
        out += kCommonFooter;
        return out;
    }

  private:
    std::map<std::string, std::string> _keys;
    std::map<std::string, Arg> _options;
    bool _help = false;
    std::string _intro;
    std::vector<std::string> _help_lines;
};

} // namespace cli