#pragma once

#include <algorithm>
#include <cmath>

namespace pet {

enum class Mood { Idle, Happy, Eating, Playing, Sleeping, Dragging };

struct State {
    Mood mood = Mood::Idle;
    bool roaming = true;
    int direction = 1;
    double time = 0.0;
    double moodTime = 0.0;
    double travel = 0.0;

    void setMood(Mood value) {
        mood = value;
        moodTime = 0.0;
    }

    bool walking() const { return mood == Mood::Idle && roaming; }

    // Keep real elapsed time, including a delayed timer. Invalid elapsed times
    // have no effect. Sleep and dragging only end through an explicit setMood.
    void tick(double dt) {
        if (!std::isfinite(dt) || dt <= 0.0) return;

        time += dt;
        moodTime += dt;
        if (walking()) travel += dt;

        double duration = 0.0;
        switch (mood) {
            case Mood::Happy: duration = 4.0; break;
            case Mood::Eating: duration = 6.0; break;
            case Mood::Playing: duration = 5.0; break;
            default: break;
        }

        if (duration > 0.0 && moodTime >= duration) {
            const double idleTime = moodTime - duration;
            setMood(Mood::Idle);
            moodTime = idleTime;
            if (roaming) travel += idleTime;
        }
    }

    // Logical pixels above the baseline; positive values always move upward.
    double bob() const {
        switch (mood) {
            case Mood::Dragging: return 0.0;
            case Mood::Happy:
            case Mood::Playing:
                return 14.0 * std::abs(std::sin(moodTime * 5.0));
            case Mood::Sleeping:
                return 0.35 * (1.0 + std::sin(moodTime * 1.5));
            case Mood::Eating:
                return 0.75 * (1.0 + std::sin(moodTime * 5.0));
            case Mood::Idle:
                return walking() ? 5.0 * std::abs(std::sin(travel * 6.0))
                                 : 0.75 * (1.0 + std::sin(time * 2.0));
        }
        return 0.0;
    }

    double squash() const {
        switch (mood) {
            case Mood::Dragging: return 1.0;
            case Mood::Sleeping:
                return 1.0 + 0.01 * std::sin(moodTime * 1.5);
            case Mood::Happy:
            case Mood::Playing:
                return 1.0 + 0.06 * std::sin(moodTime * 10.0);
            case Mood::Eating:
                return 1.0 + 0.025 * std::sin(moodTime * 5.0);
            case Mood::Idle:
                return walking() ? 1.0 + 0.025 * std::sin(travel * 12.0)
                                 : 1.0 + 0.01 * std::sin(time * 2.0);
        }
        return 1.0;
    }
};

// Bounds are monitor/work-area coordinates, so the origin may be negative.
// A window larger than the available area is anchored to its leading edge.
inline double clampAxis(double position, double windowSize,
                        double min, double max) {
    if (std::isnan(position)) return min;
    const double upper = std::max(min, max - std::max(0.0, windowSize));
    return std::clamp(position, min, upper);
}

}  // namespace pet
