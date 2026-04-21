#ifndef GAME_1_H
#define GAME_1_H

#include "Menu.h"

#include <stdint.h>

typedef struct {
    int16_t player_x;
    int16_t player_y;
    int8_t player_row;
    int8_t player_col;
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
    uint8_t enemy_active;
    int8_t enemy_row;
    int8_t enemy_col;
    uint32_t enemy_last_move_tick;
    uint8_t enemy_dir_index;
} Game1State;

void Game1_Init(Game1State* state);
void Game1_HandleInput(Game1State* state);
void Game1_Update(Game1State* state);
void Game1_Render(Game1State* state);
static void Game1_AddExplosionCell(Game1State* state, int8_t row, int8_t col);
MenuState Game1_Run(void);

#endif // GAME_1_H
