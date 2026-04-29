#ifndef GAME_1_H
#define GAME_1_H

#include "Menu.h"
#include <stdint.h>

#define MAX_ENEMIES 3
// Frame rate for this game (in milliseconds)
#define GAME1_FRAME_TIME_MS 30  // ~33 FPS
#define MAP_ROWS 11
#define MAP_COLS 11
#define CELL_SIZE 20
#define MAP_X 10
#define MAP_Y (10 + UI_HEIGHT)
#define MOVE_COOLDOWN_MS 140
#define UI_HEIGHT 20

#define TILE_EMPTY 0
#define TILE_SOLID_WALL 1
#define TILE_BREAKABLE 2

#define BOMB_COUNTDOWN_MS 2000
#define EXPLOSION_DURATION_MS 300
#define ENEMY_MOVE_INTERVAL_MS 450

typedef struct {
    int16_t player_x;
    int16_t player_y;
    int8_t player_row;
    int8_t player_col;
    uint8_t player_hp;
    uint32_t player_last_hit_tick;    
    int8_t move_row_delta;
    int8_t move_col_delta;
    uint8_t move_requested;
    uint32_t last_move_tick;
    uint8_t map[11][11];
    uint8_t bomb_active;
    int8_t bomb_row;
    int8_t bomb_col;
    uint32_t bomb_placed_tick;
    uint8_t bomb_trigger_ready;
    uint8_t explosion_active;
    uint32_t explosion_start_tick;
    uint8_t explosion_count;
    int8_t explosion_rows[5];
    int8_t explosion_cols[5];
    uint8_t player_alive;
    uint8_t game_over;
    uint8_t game_win;

    uint8_t enemy_active[MAX_ENEMIES];
    int8_t enemy_row[MAX_ENEMIES];
    int8_t enemy_col[MAX_ENEMIES];
    uint32_t enemy_last_move_tick[MAX_ENEMIES];

    uint16_t score;
} Game1State;

void Game1_Init(Game1State* state);
void Game1_HandleInput(Game1State* state);
void Game1_Update(Game1State* state);
void Game1_Render(Game1State* state);
static void Game1_AddExplosionCell(Game1State* state, int8_t row, int8_t col);
MenuState Game1_Run(void);



void Update_Player(Game1State* state, uint32_t now);
uint8_t Handle_Enemy_Collision(Game1State* state, uint32_t now);
void Update_Bomb(Game1State* state, uint32_t now);
void Generate_Explosion(Game1State* state, uint32_t now);
void Handle_Explosion_Damage(Game1State* state, uint32_t now);
void Update_Enemy(Game1State* state, uint32_t now);
void Update_Explosion_Lifecycle(Game1State* state, uint32_t now);
void Game1_SpawnEnemy(Game1State* state);
#endif // GAME_1_H
