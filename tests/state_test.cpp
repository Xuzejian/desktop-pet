#include "../src/pet_state.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
int failures = 0;

void check(bool condition, const char* description) {
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

bool near(double a, double b) { return std::abs(a - b) < 1e-9; }

void actionDuration(pet::Mood mood, double duration) {
    pet::State state;
    state.setMood(mood);
    check(!state.walking(), "actions suspend roaming");
    state.tick(duration - 0.25);
    check(state.mood == mood, "action persists until its deadline");
    check(near(state.travel, 0.0), "actions do not accumulate travel");
    state.tick(0.25);
    check(state.mood == pet::Mood::Idle, "action completes at its deadline");
    check(near(state.moodTime, 0.0), "new idle state starts at zero");
    check(state.walking(), "roaming resumes after an action");
}
}  // namespace

int main() {
    using pet::Mood;
    using pet::State;

    State state;
    check(state.mood == Mood::Idle && state.walking(), "default pet roams");
    state.tick(2.0);
    check(near(state.time, 2.0) && near(state.moodTime, 2.0),
          "tick advances both clocks by elapsed time");
    check(near(state.travel, 2.0), "walking accumulates travel time");

    state.roaming = false;
    state.tick(3.0);
    check(!state.walking() && near(state.travel, 2.0), "paused roaming stops travel");
    state.setMood(Mood::Happy);
    check(near(state.moodTime, 0.0) && near(state.time, 5.0),
          "changing mood resets only its clock");
    state.tick(1.0);
    state.setMood(Mood::Happy);
    check(near(state.moodTime, 0.0), "repeating an interaction restarts its action");
    state.tick(10.0);
    check(state.mood == Mood::Idle && !state.walking(),
          "finishing an action preserves the paused roaming preference");
    check(near(state.travel, 2.0), "paused idle time does not accumulate travel");

    actionDuration(Mood::Happy, 4.0);
    actionDuration(Mood::Eating, 6.0);
    actionDuration(Mood::Playing, 5.0);

    State delayed;
    delayed.setMood(Mood::Eating);
    delayed.tick(10.0);
    check(delayed.mood == Mood::Idle && near(delayed.time, 10.0),
          "delayed timer finishes actions without clamping elapsed time");
    check(near(delayed.moodTime, 4.0) && near(delayed.travel, 4.0),
          "only time after an action deadline becomes idle travel");

    for (Mood persistent : {Mood::Sleeping, Mood::Dragging}) {
        State resting;
        resting.setMood(persistent);
        resting.tick(86400.0);
        check(resting.mood == persistent, "sleep and dragging never time out");
        check(!resting.walking() && near(resting.travel, 0.0),
              "persistent states do not roam");
    }

    State invalid;
    invalid.setMood(Mood::Playing);
    invalid.tick(1.0);
    for (double dt : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::infinity(),
                      -std::numeric_limits<double>::infinity()}) {
        invalid.tick(dt);
        check(invalid.mood == Mood::Playing && near(invalid.time, 1.0) &&
                  near(invalid.moodTime, 1.0) && near(invalid.travel, 0.0),
              "invalid elapsed time leaves all state unchanged");
    }

    // Sample animation curves through several cycles and both roaming modes.
    for (Mood mood : {Mood::Idle, Mood::Happy, Mood::Eating, Mood::Playing,
                      Mood::Sleeping, Mood::Dragging}) {
        for (bool roaming : {false, true}) {
            State animated;
            animated.setMood(mood);
            animated.roaming = roaming;
            const double upperBob = mood == Mood::Happy || mood == Mood::Playing
                                        ? 14.0
                                        : mood == Mood::Idle && roaming ? 5.0 : 1.5;
            for (int sample = 0; sample < 1000; ++sample) {
                // Sample directly so short actions are covered for all phases.
                animated.time = animated.moodTime = animated.travel = sample * 0.017;
                check(animated.bob() >= 0.0 && animated.bob() <= upperBob,
                      "bob stays above the baseline within the promised range");
                check(animated.squash() >= 0.94 && animated.squash() <= 1.06,
                      "squash remains modest");
                if (mood == Mood::Sleeping) {
                    check(animated.squash() >= 0.99 && animated.squash() <= 1.01,
                          "sleep breathing stays within one percent");
                }
                if (mood == Mood::Dragging) {
                    check(near(animated.bob(), 0.0) && near(animated.squash(), 1.0),
                          "dragged pet stays visually still");
                }
            }
        }
    }

    using pet::clampAxis;
    check(near(clampAxis(300.0, 200.0, 0.0, 1920.0), 300.0),
          "window inside a monitor keeps its position");
    check(near(clampAxis(-20.0, 200.0, 0.0, 1920.0), 0.0),
          "window cannot leave the leading edge");
    check(near(clampAxis(1900.0, 200.0, 0.0, 1920.0), 1720.0),
          "window cannot leave the trailing edge");
    check(near(clampAxis(-1000.0, 200.0, -1920.0, 0.0), -1000.0),
          "negative monitor origin is retained");
    check(near(clampAxis(-2100.0, 200.0, -1920.0, 0.0), -1920.0),
          "negative monitor leading edge clamps correctly");
    check(near(clampAxis(10.0, 200.0, -1920.0, 0.0), -200.0),
          "negative monitor trailing edge accounts for window size");
    check(near(clampAxis(10.0, 3000.0, -1920.0, 0.0), -1920.0),
          "oversized window anchors to leading edge");
    check(near(clampAxis(10.0, 200.0, -10.0, -10.0), -10.0),
          "zero-width work area is safe");
    check(near(clampAxis(10.0, 200.0, 50.0, -10.0), 50.0),
          "reversed bounds anchor safely");
    check(near(clampAxis(10.0, -200.0, 0.0, 20.0), 10.0),
          "negative window size is treated as zero");
    check(near(clampAxis(std::numeric_limits<double>::quiet_NaN(), 200.0,
                         -1920.0, 0.0), -1920.0),
          "invalid position recovers to monitor origin");

    if (failures != 0) {
        std::cerr << failures << " checks failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All desktop pet state tests passed.\n";
    return EXIT_SUCCESS;
}
