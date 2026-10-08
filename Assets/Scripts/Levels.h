#pragma once

#include "Shipfulness.h"

namespace ship {

struct ControlPoint {
    float x, z, halfWidth;
};

enum class ObstacleKind { Barge, Bridge, Pillar, Pier };

struct Obstacle {
    ObstacleKind kind;
    float at;
    float side;
    float reach;
    float length;
};

struct LevelDef {
    const char* name;
    const char* subtitle;
    float parTime;
    unsigned seed;
    std::vector<ControlPoint> points;
    std::vector<Obstacle> obstacles;
    std::vector<std::pair<float, float>> basins;
};

inline const std::vector<LevelDef>& Levels() {
    static const std::vector<LevelDef> levels = {
        {"GRAND CANAL",
         "A RELAXING CRUISE. ALMOST.",
         85.0f,
         7,
         {{0, -20, 8}, {0, 20, 7}, {0, 60, 6.5f}, {12, 105, 6.5f}, {38, 140, 6.5f}, {46, 185, 6},
          {38, 230, 6}, {10, 268, 6.5f}, {-6, 310, 7}, {-8, 350, 9}, {-8, 400, 10}},
         {{ObstacleKind::Bridge, 0.30f, 0, 0.9f, 5},
          {ObstacleKind::Barge, 0.47f, 1, 2.2f, 14},
          {ObstacleKind::Bridge, 0.62f, 0, 0.9f, 5},
          {ObstacleKind::Barge, 0.76f, -1, 2.0f, 12}},
         {{0.17f, 15.0f}, {0.55f, 17.0f}}},
        {"THE ELBOWS",
         "TWO CORNERS NOBODY DESIGNED FOR SHIPS.",
         120.0f,
         21,
         {{0, -20, 8}, {0, 30, 6.5f}, {0, 80, 6.5f}, {4, 100, 8}, {20, 110, 8}, {50, 112, 6.5f},
          {85, 112, 6.5f}, {101, 118, 8}, {108, 135, 8}, {108, 175, 6}, {108, 210, 6}, {100, 228, 8},
          {80, 236, 8}, {50, 238, 6.5f}, {30, 245, 8}, {22, 262, 8}, {20, 300, 7}, {20, 345, 9},
          {20, 395, 10}},
         {{ObstacleKind::Pier, 0.14f, -1, 2.5f, 3},
          {ObstacleKind::Bridge, 0.40f, 0, 0.8f, 5},
          {ObstacleKind::Barge, 0.55f, 1, 1.8f, 12},
          {ObstacleKind::Pier, 0.70f, 1, 2.5f, 3},
          {ObstacleKind::Bridge, 0.86f, 0, 0.8f, 5}},
         {{0.26f, 16.0f}, {0.63f, 15.0f}}},
        {"THE NARROWS",
         "CITY HALL SWEARS IT FITS.",
         150.0f,
         33,
         {{0, -20, 7}, {0, 25, 5.5f}, {-10, 65, 5}, {-34, 95, 5}, {-40, 135, 5}, {-28, 170, 5},
          {0, 190, 5.5f}, {30, 200, 5.5f}, {52, 225, 5}, {55, 265, 4.8f}, {40, 300, 5}, {12, 320, 5.5f},
          {-8, 345, 6}, {-14, 380, 8}, {-14, 430, 9.5f}},
         {{ObstacleKind::Pillar, 0.12f, 0, 0.9f, 0.9f},
          {ObstacleKind::Barge, 0.25f, -1, 1.6f, 12},
          {ObstacleKind::Bridge, 0.36f, 0, 0.9f, 5},
          {ObstacleKind::Barge, 0.50f, 1, 1.5f, 14},
          {ObstacleKind::Pier, 0.58f, -1, 2.0f, 3},
          {ObstacleKind::Bridge, 0.66f, 0, 1.0f, 5},
          {ObstacleKind::Barge, 0.80f, -1, 1.5f, 10},
          {ObstacleKind::Pillar, 0.90f, 0, 0.9f, 0.9f}},
         {{0.43f, 14.0f}, {0.73f, 15.0f}}},
    };
    return levels;
}

}
