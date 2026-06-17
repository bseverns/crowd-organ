#pragma once

#include <string>

class StubLogStream {
public:
    explicit StubLogStream(const char* label) : label(label) {}
    explicit StubLogStream(const std::string& label) : label(label) {}

    template <typename T>
    StubLogStream& operator<<(const T& value) {
        (void)value;
        return *this;
    }

    ~StubLogStream() = default;

private:
    std::string label;
};

inline StubLogStream ofLogNotice() {
    return StubLogStream("notice");
}

inline StubLogStream ofLogNotice(const std::string& label) {
    return StubLogStream(label);
}

inline StubLogStream ofLogWarning() {
    return StubLogStream("warning");
}

inline StubLogStream ofLogWarning(const std::string& label) {
    return StubLogStream(label);
}

inline StubLogStream ofLogError(const std::string& label) {
    return StubLogStream(label);
}
