#pragma once

#include <framework/disable_all_warnings.h>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
DISABLE_WARNINGS_PUSH()
#include <glm/gtc/quaternion.hpp>
#include <glm/fwd.hpp>
DISABLE_WARNINGS_POP()

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

    void addTransformations(std::vector<float> time, std::vector<T> transformation) {
        if(time.size() != transformation.size()) throw std::logic_error("Weird, the vectors have different size. This usually does not happen.");

        for(int i = 0; i < time.size(); i++) {
            transformations[time[i]] = transformation[i];
        }
    }

    void setInterpolationMode(const std::string& im) {
        interpolationMode = im;
    }

    T getTransformation(float animTime) {
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

    float animationDuration() {
        return std::prev(transformations.end())->first;
    }
};

