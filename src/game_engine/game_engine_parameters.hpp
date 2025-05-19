#ifndef _GAME_ENGINE_PARAMETERS_HPP_
#define _GAME_ENGINE_PARAMETERS_HPP_

// ----- MAP -----
extern int MAP_HEIGHT; // 800
extern int MAP_WIDTH; // 1200

// ----- Game Ticks -----
extern int GAME_TICKS_PER_SECOND; // 3
extern float GAME_TICK_PERIOD_SEC;
extern int GAME_TICK_PERIOD_MS;  // for setInterval on front-end
extern float GAME_TICK_PERIOD_US;

// ----- Radius -----
extern int PLAYER_RADIUS; // 10.0
extern int PLAYER_ON_PLAYER_COLLISION;
extern int PLAYER_ON_WALL_COLLISION;

// ----- Spatial Partition -----
extern int SPATIAL_PARTITION_COLS; // 8
extern int SPATIAL_PARTITION_ROWS; // 8
extern int SPATIAL_PARTITION_COUNT;
extern int PARTITION_WIDTH;
extern int PARTITION_HEIGHT;

// ----- Workers -----
extern int WORKER_COUNT;



void initialize_constants(int map_height,
                          int map_width,
                          int game_ticks_per_second,
                          int player_radius,
                          int spatial_partition_cols,
                          int spatial_parition_rows,
                          int worker_count_);

#endif
