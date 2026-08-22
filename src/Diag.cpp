#include "Diag.h"

namespace shalimar {

std::string Message::formatted() const {
    std::string out = severity == Severity::Error ? "Error: " : "Warning: ";
    if (line > 0) {
        out += "line ";
        out += std::to_string(line);
        out += ": ";
    }
    out += text;
    return out;
}

void Diagnostics::error(int line, const std::string& text) {
    messages_.push_back(Message{Severity::Error, line, text});
    ++errors_;
}

void Diagnostics::warning(int line, const std::string& text) {
    messages_.push_back(Message{Severity::Warning, line, text});
}

void Diagnostics::unsupported(int line, const std::string& text) {
    unsupported_.push_back(Message{Severity::Error, line, text});
}

void Diagnostics::writeTo(std::string& out) const {
    for (const Message& m : messages_) {
        out += m.formatted();
        out += '\n';
    }
}

}  // namespace shalimar
