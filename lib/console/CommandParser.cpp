#include "CommandParser.h"

#include <cstdlib>
#include <cstring>

namespace anita {

namespace {

struct Range {
    float lo, hi;
};

bool parseFloat(const char* tok, float& out, Range r) {
    if (!tok) return false;
    char* end = nullptr;
    const float v = std::strtof(tok, &end);
    if (end == tok || (end && *end != '\0')) return false;
    if (v < r.lo || v > r.hi) return false;
    out = v;
    return true;
}

Command invalid(const char* why) {
    Command c;
    c.type = Command::Type::Invalid;
    c.error = why;
    return c;
}

}  // namespace

Command CommandParser::parse(const char* line) {
    Command cmd;
    if (!line) return cmd;

    // Tokenize a local copy (strtok mutates).
    char buf[96];
    std::strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* save = nullptr;
    const char* t0 = strtok_r(buf, " \t\r\n", &save);
    if (!t0) return cmd;  // Type::None

    if (std::strcmp(t0, "get") == 0) {
        cmd.type = Command::Type::Get;
        return cmd;
    }

    if (std::strcmp(t0, "mark") == 0) {
        const char* rest = strtok_r(nullptr, "\r\n", &save);
        if (!rest || *rest == '\0') return invalid("mark: missing text");
        while (*rest == ' ' || *rest == '\t') ++rest;
        std::strncpy(cmd.text, rest, sizeof(cmd.text) - 1);
        cmd.type = Command::Type::Mark;
        return cmd;
    }

    if (std::strcmp(t0, "id") == 0) {
        const char* sub = strtok_r(nullptr, " \t\r\n", &save);
        if (!sub) return invalid("id: duty|off|stop");
        if (std::strcmp(sub, "stop") == 0) {
            cmd.type = Command::Type::IdStop;
            return cmd;
        }
        if (std::strcmp(sub, "duty") == 0) {
            const char* d = strtok_r(nullptr, " \t\r\n", &save);
            const char* s = strtok_r(nullptr, " \t\r\n", &save);
            if (!parseFloat(d, cmd.value, {0.0f, 1.0f}))
                return invalid("id duty: duty must be 0..1");
            if (!parseFloat(s, cmd.seconds, {1.0f, 3600.0f}))
                return invalid("id duty: seconds must be 1..3600");
            cmd.type = Command::Type::IdDuty;
            return cmd;
        }
        if (std::strcmp(sub, "off") == 0) {
            const char* s = strtok_r(nullptr, " \t\r\n", &save);
            if (!parseFloat(s, cmd.seconds, {1.0f, 3600.0f}))
                return invalid("id off: seconds must be 1..3600");
            cmd.type = Command::Type::IdOff;
            return cmd;
        }
        return invalid("id: duty|off|stop");
    }

    if (std::strcmp(t0, "set") == 0) {
        const char* p = strtok_r(nullptr, " \t\r\n", &save);
        const char* v = strtok_r(nullptr, " \t\r\n", &save);
        if (!p || !v) return invalid("set <param> <value>");

        struct Entry {
            const char* name;
            Command::Param param;
            Range range;
        };
        static constexpr Entry kTable[] = {
            {"b0", Command::Param::B0Gain, {0.05f, 5.0f}},
            {"wc", Command::Param::Wc, {0.001f, 1.0f}},
            {"wo", Command::Param::Wo, {0.001f, 5.0f}},
            {"pred", Command::Param::Pred, {0.0f, 120.0f}},
            {"kboost", Command::Param::KBoost, {0.0f, 10.0f}},
            {"cap", Command::Param::Cap, {0.0f, 1.0f}},
            {"setpoint", Command::Param::Setpoint, {85.0f, 98.0f}},
        };
        for (const auto& e : kTable) {
            if (std::strcmp(p, e.name) == 0) {
                if (!parseFloat(v, cmd.value, e.range))
                    return invalid("set: value out of range");
                cmd.type = Command::Type::Set;
                cmd.param = e.param;
                return cmd;
            }
        }
        return invalid("set: unknown param");
    }

    return invalid("unknown command");
}

}  // namespace anita
