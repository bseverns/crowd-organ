#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>

template <typename T>
T ofClamp(T value, T minValue, T maxValue) {
    return std::max(minValue, std::min(value, maxValue));
}

template <typename T>
std::string ofToString(const T& value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

namespace glm {
struct vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    vec3() = default;
    explicit vec3(float value) : x(value), y(value), z(value) {}
    vec3(float xValue, float yValue, float zValue) : x(xValue), y(yValue), z(zValue) {}
};

inline vec3 operator-(const vec3& lhs, const vec3& rhs) {
    return vec3(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z);
}

inline vec3 operator+(const vec3& lhs, const vec3& rhs) {
    return vec3(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z);
}

inline vec3 operator*(const vec3& value, float scalar) {
    return vec3(value.x * scalar, value.y * scalar, value.z * scalar);
}

inline vec3 operator/(const vec3& value, float divisor) {
    return vec3(value.x / divisor, value.y / divisor, value.z / divisor);
}

inline float length(const vec3& value) {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}
} // namespace glm
