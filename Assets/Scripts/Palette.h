#pragma once

namespace ship {

struct Rgb {
    float r, g, b;
};

constexpr Rgb kWater{0.16f, 0.43f, 0.57f};
constexpr Rgb kShallow{0.34f, 0.64f, 0.74f};
constexpr Rgb kFoam{0.93f, 0.97f, 0.98f};
constexpr Rgb kTerracotta{0.74f, 0.42f, 0.33f};
constexpr Rgb kTerracottaDark{0.60f, 0.32f, 0.26f};
constexpr Rgb kGoal{0.55f, 0.90f, 0.55f};

constexpr Rgb kWalls[] = {
    {0.96f, 0.94f, 0.88f}, {0.93f, 0.88f, 0.76f}, {0.78f, 0.47f, 0.38f}, {0.99f, 0.97f, 0.95f},
    {0.84f, 0.84f, 0.86f}, {0.91f, 0.80f, 0.62f}, {0.70f, 0.40f, 0.33f},
};
constexpr Rgb kRoofs[] = {
    {0.80f, 0.33f, 0.33f}, {0.55f, 0.38f, 0.27f}, {0.97f, 0.97f, 0.97f}, {0.72f, 0.28f, 0.30f},
    {0.62f, 0.62f, 0.66f},
};

constexpr Rgb kHull{0.97f, 0.97f, 0.96f};
constexpr Rgb kHullStripe{0.85f, 0.30f, 0.28f};
constexpr Rgb kDeck{0.80f, 0.80f, 0.80f};
constexpr Rgb kFunnel{0.25f, 0.25f, 0.28f};

}
