#pragma once
#include <map>
#include <stdexcept>
#include <string>
#include <glm/gtc/quaternion.hpp>
#include <glm/fwd.hpp>
#include <glm/ext/matrix_transform.hpp>

template<typename T>
T interpolate(const T& before, const T& after, float value);

template<>
inline glm::vec3 interpolate(const glm::vec3& before, const glm::vec3& after, const float value) {
    return glm::mix(before, after, value); //Linear interpolation for vectors
}

template<>
inline glm::quat interpolate(const glm::quat& before, const glm::quat& after, const float value) {
    return glm::slerp(before, after, value); //Spherical linear interpolation for quats
}

template<typename T>
class TransformationChannel {
    std::map<float, T> transformations;
    std::string interpolationMode;

    void getBeforeAndAfter(float time, std::pair<float, T>& before, std::pair<float, T>& after) {
        auto lower = transformations.lower_bound(time);
        auto upper = transformations.upper_bound(time);

        auto prev = std::prev(lower);
        auto next = upper;

        before = *prev;
        after = *next;
    }

public:
    TransformationChannel() = default;

    void addTransformation(float time, T transformation) {
        transformations[time] = transformation;
    }

    void setInterpolationMode(const std::string& im) {
        interpolationMode = im;
    }

    T getTransformation(float currentTime) {
        //Compute time in animation time
        float animDuration = std::prev(transformations.end())->first;
        float animTime = std::fmod(currentTime, animDuration);

        if(transformations.contains(animTime)) return transformations[animTime]; //Immediately return transformation if map contains it

        //Otherwise we need to interpolate between the two entries that is directly before and after the animation time
        std::pair<float, T> before, after;
        getBeforeAndAfter(animTime, before, after);

        if(interpolationMode == "STEP") {
            return transformations[before.first]; //Not really an interpolation style, just return transformation at `before`
        } if(interpolationMode == "LINEAR") {
            auto interpolatedTime = (animTime - before.first) / (after.first - before.first);
            return interpolate(before.second, after.second, interpolatedTime);
        } if(interpolationMode == "CUBICSPLINE") {
            throw std::invalid_argument("Application does not support this animation type. Please add.");
        }

        throw std::runtime_error("There are only 3 interpolation modes, so code should never reach this.");
    }
};

