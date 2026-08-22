// Diagnostics.
//
// Every message names its line and takes one of two forms:
//
//     Error: line 3: ...          the program does not run
//     Warning: line 3: ...        the program runs anyway
//
// There is deliberately no stage name in the text. Which stage caught a
// problem is an implementation detail, and the vocabulary of a compiler's
// internals has no place in a message aimed at someone writing a program.
// For the same reason nothing here quotes a token kind or an AST node type.
//
// Messages are kept short on purpose: the app's console line holds about 47
// characters, and this compiler answers to the same document the app does.
#pragma once


#include <string>
#include <vector>

namespace shalimar {

enum class Severity { Error, Warning };

struct Message {
    Severity severity;
    int line;                   // 0 means the program as a whole
    std::string text;

    std::string formatted() const;
};

// The checker does not stop at the first problem, so diagnostics accumulate
// rather than throw. Errors and warnings share one list and one order: they
// are reported in the order they were found, not sorted by severity.
class Diagnostics {
public:
    void error(int line, const std::string& text);
    void warning(int line, const std::string& text);

    bool hasErrors() const { return errors_ > 0; }
    const std::vector<Message>& messages() const { return messages_; }
    void writeTo(std::string& out) const;

private:
    std::vector<Message> messages_;
    int errors_ = 0;
};

}  // namespace shalimar
