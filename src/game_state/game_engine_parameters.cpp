#include <cassert>

#include "boost-log.hpp"

#include "game_engine_parameters.hpp"

// ----- MAP -----
int MAP_HEIGHT; // 800
int MAP_WIDTH; // 1200

// ----- Game Ticks -----
int GAME_TICKS_PER_SECOND; // 3
float GAME_TICK_PERIOD_SEC;
float GAME_TICK_PERIOD_US;

// ----- Radius -----
int PLAYER_RADIUS; // 10.0
int PLAYER_ON_PLAYER_COLLISION;
int PLAYER_ON_WALL_COLLISION;

// ----- Spatial Partition -----
int SPATIAL_PARTITION_COLS; // 8
int SPATIAL_PARTITION_ROWS; // 8
int SPATIAL_PARTITION_COUNT;
int PARTITION_WIDTH;
int PARTITION_HEIGHT;



void initialize_constants(int map_height,
                          int map_width,
                          int game_ticks_per_second,
                          int player_radius,
                          int spatial_partition_cols,
                          int spatial_partition_rows)
{
  // ----- Map -----
  MAP_HEIGHT = map_height;
  MAP_WIDTH  = map_width;
  
  // ----- Game Ticks -----
  GAME_TICKS_PER_SECOND = game_ticks_per_second;
  GAME_TICK_PERIOD_SEC = 1.0 / GAME_TICKS_PER_SECOND;
  GAME_TICK_PERIOD_US = GAME_TICK_PERIOD_SEC * 1000000.0;
  TRACE << "Game ticks per second: " << GAME_TICKS_PER_SECOND << std::endl
        << "MicroSecond period: " << GAME_TICK_PERIOD_US;

  // ----- Radius -----
  PLAYER_RADIUS = player_radius;
  PLAYER_ON_PLAYER_COLLISION = PLAYER_RADIUS * 2;
  PLAYER_ON_WALL_COLLISION = PLAYER_RADIUS;

  // ----- Spatial Partition  -----
  SPATIAL_PARTITION_COLS = spatial_partition_cols;
  SPATIAL_PARTITION_ROWS = spatial_partition_rows;
  SPATIAL_PARTITION_COUNT = SPATIAL_PARTITION_COLS * SPATIAL_PARTITION_ROWS;
  PARTITION_HEIGHT = MAP_HEIGHT / SPATIAL_PARTITION_ROWS;
  PARTITION_WIDTH  = MAP_WIDTH  / SPATIAL_PARTITION_COLS;

  TRACE << "Partition rows and cols: " << SPATIAL_PARTITION_ROWS << " " << SPATIAL_PARTITION_COLS;

  assert(MAP_HEIGHT % SPATIAL_PARTITION_ROWS == 0);
  assert(MAP_WIDTH  % SPATIAL_PARTITION_COLS == 0);

}
