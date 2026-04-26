#include "Game_1.h"
#include "InputHandler.h"
#include "Menu.h"
#include "LCD.h"
#include "PWM.h"
#include "Buzzer.h"
#include "stm32l4xx_hal.h"
#include <stdio.h>
#include "Joystick.h"  // Joystick input
#include  "main.h"
#include <stdlib.h>

extern ST7789V2_cfg_t cfg0;
extern PWM_cfg_t pwm_cfg;      // LED PWM control
extern Buzzer_cfg_t buzzer_cfg; // Buzzer control

extern Joystick_cfg_t joystick_cfg;
extern Joystick_t joystick_data;

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


void Game1_Init(Game1State* state) {
    srand(HAL_GetTick());
    for (int r = 0; r < MAP_ROWS; r++) {
        for (int c = 0; c < MAP_COLS; c++) {
            if (r == 0 || r == MAP_ROWS - 1 || c == 0 || c == MAP_COLS - 1) {
                state->map[r][c] = TILE_SOLID_WALL;
            }
            else if (r % 2 == 0 && c % 2 == 0) {
                state->map[r][c] = TILE_SOLID_WALL;
            }
            else {
                if (rand() % 100 < 30) {
                    state->map[r][c] = TILE_BREAKABLE;
                } else {
                    state->map[r][c] = TILE_EMPTY;
                }
            }
        }
    }
    state->map[1][1] = TILE_EMPTY;
    state->map[1][2] = TILE_EMPTY;
    state->map[2][1] = TILE_EMPTY;

    state->player_row = 1;
    state->player_col = 1;
    state->player_x = MAP_X + state->player_col * CELL_SIZE;
    state->player_y = MAP_Y + state->player_row * CELL_SIZE;

    state->player_hp = 3;
    state->player_alive = 1;
    state->game_over = 0;
    state->game_win = 0;
    state->score = 0;

    state->move_requested = 0;
    state->last_move_tick = HAL_GetTick();
    state->bomb_active = 0;
    state->explosion_active = 0;

    for (int i = 0; i < MAX_ENEMIES; i++) {

        int r, c;

        // Find a valid empty position
        do {
            r = rand() % (MAP_ROWS - 2) + 1;
            c = rand() % (MAP_COLS - 2) + 1;
        } while (
            state->map[r][c] != TILE_EMPTY ||
            (r == state->player_row && c == state->player_col)
        );
        state->enemy_active[i] = 1;
        state->enemy_row[i] = r;
        state->enemy_col[i] = c;
        state->enemy_last_move_tick[i] = HAL_GetTick() + rand() % 200;
    }
}

void Game1_HandleInput(Game1State* state) {

    Joystick_Read(&joystick_cfg, &joystick_data);

    state->move_row_delta = 0;
    state->move_col_delta = 0;
    state->move_requested = 0;

    if (state->game_over || state->game_win) {
        if (current_input.btn2_pressed) {
            Game1_Init(state);
        }
        return;
    }

    int dir = joystick_data.direction;

    if (dir == N || dir == NE || dir == NW) {
        state->move_row_delta = -1;
        state->move_requested = 1;
    }
    else if (dir == S || dir == SE || dir == SW) {
        state->move_row_delta = 1;
        state->move_requested = 1;
    }
    else if (dir == W) {
        state->move_col_delta = -1;
        state->move_requested = 1;
    }
    else if (dir == E) {
        state->move_col_delta = 1;
        state->move_requested = 1;
    }

    if (current_input.btn2_pressed) {

        if (!state->bomb_active) {
            state->bomb_active = 1;
            state->bomb_row = state->player_row;
            state->bomb_col = state->player_col;
            state->bomb_placed_tick = HAL_GetTick();
        }
    }
}

void Game1_Update(Game1State* state) {
    uint32_t now = HAL_GetTick();

    if (state->game_over || state->game_win)
        return;

    Update_Player(state, now);

    if (Handle_Enemy_Collision(state, now))
        return;

    Update_Bomb(state, now);
    Generate_Explosion(state, now);
    Handle_Explosion_Damage(state, now);

    if (state->game_over)
        return;

    Update_Enemy(state, now);

    Update_Explosion_Lifecycle(state, now);
}

void Game1_Render(Game1State* state) {
    LCD_Fill_Buffer(0);

    LCD_Draw_Rect(0, 0, 240, UI_HEIGHT, 8, 1);   // top bar background

    for (int r = 0; r < MAP_ROWS; r++) {
        for (int c = 0; c < MAP_COLS; c++) {

            uint16_t x = MAP_X + c * CELL_SIZE;
            uint16_t y = MAP_Y + r * CELL_SIZE;

            // base cell (background)
            LCD_Draw_Rect(x, y, CELL_SIZE, CELL_SIZE, 0, 1);

            if (state->map[r][c] == TILE_SOLID_WALL) {
                // filled wall
                LCD_Draw_Rect(x+2, y+2, CELL_SIZE-4, CELL_SIZE-4, 1, 1);
            }
            else if (state->map[r][c] == TILE_BREAKABLE) {
                // breakable block (lighter)
                LCD_Draw_Rect(x+3, y+3, CELL_SIZE-6, CELL_SIZE-6, 5, 1);
            }
        }
    }
    if (state->bomb_active) {
        uint16_t x = MAP_X + state->bomb_col * CELL_SIZE;
        uint16_t y = MAP_Y + state->bomb_row * CELL_SIZE;

        LCD_Draw_Rect(x+6, y+6, CELL_SIZE-12, CELL_SIZE-12, 6, 1);
    }

    if (state->explosion_active) {
        for (int i = 0; i < state->explosion_count; i++) {

            uint16_t x = MAP_X + state->explosion_cols[i] * CELL_SIZE;
            uint16_t y = MAP_Y + state->explosion_rows[i] * CELL_SIZE;

            LCD_Draw_Rect(x+2, y+2, CELL_SIZE-4, CELL_SIZE-4, 2, 1);
        }
    }

    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (state->enemy_active[i]) {

            uint16_t x = MAP_X + state->enemy_col[i] * CELL_SIZE;
            uint16_t y = MAP_Y + state->enemy_row[i] * CELL_SIZE;

            LCD_Draw_Rect(x+4, y+4, CELL_SIZE-8, CELL_SIZE-8, 7, 1);
        }
    }

    LCD_Draw_Rect(
        state->player_x + 3,
        state->player_y + 3,
        CELL_SIZE - 6,
        CELL_SIZE - 6,
        3,
        1
    );

    char str[20];
    sprintf(str, "HP:%d", state->player_hp);
    LCD_printString(str, 5, 5, 1, 1);
    sprintf(str, "Score:%d", state->score);
    LCD_printString(str, 140, 5, 1, 1);

    if (state->game_over) {
        LCD_printString("GAME OVER", 70, 95, 2, 2);
        LCD_printString("BT2: Restart", 60, 120, 1, 1);
    }
    else if (state->game_win) {
        LCD_printString("YOU WIN!", 75, 95, 3, 2);
        LCD_printString("BT2: Restart", 60, 120, 1, 1);
    }
    LCD_Refresh(&cfg0);
}

MenuState Game1_Run(void) {
    Game1State state;
    Game1_Init(&state);
    srand(HAL_GetTick());

    // Play a brief startup sound
    buzzer_tone(&buzzer_cfg, 1000, 30);  // 1kHz at 30% volume
    HAL_Delay(50);  // Brief beep duration
    buzzer_off(&buzzer_cfg);  // Stop the buzzer
    
    MenuState exit_state = MENU_STATE_HOME;  // Default: return to menu
    
    // Game's own loop - runs until exit condition
    while (1) {
        uint32_t frame_start = HAL_GetTick();
        
        // Input
        Input_Read();
        Game1_HandleInput(&state);
        
        // Check if button was pressed to return to menu
        if (current_input.btn3_pressed) {
            PWM_SetDuty(&pwm_cfg, 50);  // Reset LED to 50% when returning
            exit_state = MENU_STATE_HOME;
            break;  // Exit game loop
        }

        // Update + Render
        Game1_Update(&state);
        Game1_Render(&state);
        
        // Frame timing - wait for remainder of frame time
        uint32_t frame_time = HAL_GetTick() - frame_start;
        if (frame_time < GAME1_FRAME_TIME_MS) {
            HAL_Delay(GAME1_FRAME_TIME_MS - frame_time);
        }
    }
    
    return exit_state;  // Tell main where to go next
}

static void Game1_AddExplosionCell(Game1State* state, int8_t row, int8_t col) {
    if (state->explosion_count >= 5) {
        return;
    }
    state->explosion_rows[state->explosion_count] = row;
    state->explosion_cols[state->explosion_count] = col;
    state->explosion_count++;
}

void Update_Player(Game1State* state, uint32_t now) {
    if (state->move_requested && (now - state->last_move_tick >= MOVE_COOLDOWN_MS)) {

        int8_t target_row = state->player_row + state->move_row_delta;
        int8_t target_col = state->player_col + state->move_col_delta;

        if (target_row >= 0 && target_row < MAP_ROWS &&
            target_col >= 0 && target_col < MAP_COLS) {

            if (state->map[target_row][target_col] == TILE_EMPTY) {
                state->player_row = target_row;
                state->player_col = target_col;
                state->player_x = MAP_X + (state->player_col * CELL_SIZE);
                state->player_y = MAP_Y + (state->player_row * CELL_SIZE);
            }
        }

        state->last_move_tick = now;
    }
}



uint8_t Handle_Enemy_Collision(Game1State* state, uint32_t now) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (state->enemy_active[i] &&
            state->player_row == state->enemy_row[i] &&
            state->player_col == state->enemy_col[i]) {

            if (now - state->player_last_hit_tick > 500) {
                state->player_hp--;
                state->player_last_hit_tick = now;

                if (state->player_hp == 0) {
                    state->player_alive = 0;
                    state->game_over = 1;
                }
            }
            return 1;
        }
    }
    return 0;
}

void Update_Bomb(Game1State* state, uint32_t now) {
    if (state->bomb_active &&
        (now - state->bomb_placed_tick >= BOMB_COUNTDOWN_MS)) {

        state->bomb_active = 0;
        state->bomb_trigger_ready = 1;
    }
}

void Generate_Explosion(Game1State* state, uint32_t now) {
    if (!state->bomb_trigger_ready)
        return;

    buzzer_tone(&buzzer_cfg, 1500, 50);
    HAL_Delay(30);
    buzzer_off(&buzzer_cfg);

    int8_t center_row = state->bomb_row;
    int8_t center_col = state->bomb_col;

    state->bomb_trigger_ready = 0;
    state->bomb_row = -1;
    state->bomb_col = -1;

    state->explosion_active = 1;
    state->explosion_start_tick = now;
    state->explosion_count = 0;

    Game1_AddExplosionCell(state, center_row, center_col);

    const int8_t dir_row[4] = { -1, 1, 0, 0 };
    const int8_t dir_col[4] = { 0, 0, -1, 1 };

    for (int i = 0; i < 4; i++) {
        int8_t r = center_row + dir_row[i];
        int8_t c = center_col + dir_col[i];

        if (r < 0 || r >= MAP_ROWS || c < 0 || c >= MAP_COLS)
            continue;

        if (state->map[r][c] == TILE_SOLID_WALL)
            continue;

        Game1_AddExplosionCell(state, r, c);

        if (state->map[r][c] == TILE_BREAKABLE) {
            state->map[r][c] = TILE_EMPTY;
        }
    }
}

void Handle_Explosion_Damage(Game1State* state, uint32_t now) {
    if (!state->explosion_active)
        return;
    for (int i = 0; i < state->explosion_count; i++) {
        // Player damage
        if (state->player_row == state->explosion_rows[i] &&
            state->player_col == state->explosion_cols[i]) {

            if (now - state->player_last_hit_tick > 500) {
                state->player_hp--;
                state->player_last_hit_tick = now;

                if (state->player_hp == 0) {
                    state->player_alive = 0;
                    state->game_over = 1;
                }
            }
            break;
        }
        // Enemy destroyed
        for (int e = 0; e < MAX_ENEMIES; e++) {
            if (state->enemy_active[e] &&
                state->enemy_row[e] == state->explosion_rows[i] &&
                state->enemy_col[e] == state->explosion_cols[i]){

                state->enemy_active[e] = 0;
                state->score++;            
            }            
        }
        Game1_SpawnEnemy(state);
    }
}

void Update_Enemy(Game1State* state, uint32_t now) {
    for (int e = 0; e < MAX_ENEMIES; e++) {
        if (!state->enemy_active[e])
            continue;
        if (now - state->enemy_last_move_tick[e] < ENEMY_MOVE_INTERVAL_MS)
            continue;
        int er = state->enemy_row[e];
        int ec = state->enemy_col[e];
        int pr = state->player_row;
        int pc = state->player_col;
        int moved = 0;
        if (rand() % 2 == 0) {
            if (pr < er && state->map[er-1][ec] == TILE_EMPTY) {
                state->enemy_row[e]--;
                moved = 1;
            }
            else if(pr > er && state->map[er+1][ec] == TILE_EMPTY) {
                state->enemy_row[e]++;
                moved = 1;
            }
            else if(pc < ec && state->map[er][ec-1] == TILE_EMPTY) {
                state->enemy_col[e]--;
                moved = 1;
            }
            else if(pc > ec && state->map[er][ec+1] == TILE_EMPTY) {
                state->enemy_col[e]++;
                moved = 1;
            }
        }
        if (!moved) {
            int d = rand() % 4;
            if (d == 0 && state->map[er-1][ec] == TILE_EMPTY)
                state->enemy_row[e]--;
            else if (d == 1 && state->map[er+1][ec] == TILE_EMPTY)
                state->enemy_row[e]++;
            else if (d == 2 && state->map[er][ec-1] == TILE_EMPTY)
                state->enemy_col[e]--;
            else if (d == 3 && state->map[er][ec+1] == TILE_EMPTY)
                state->enemy_col[e]++;
        }
        state->enemy_last_move_tick[e] = now + (rand() % 100);

        if (state->player_row == state->enemy_row[e] &&
            state->player_col == state->enemy_col[e]) {

            if (now - state->player_last_hit_tick > 500) {

                state->player_hp--;
                state->player_last_hit_tick = now;
                if (state->player_hp <= 0) {
                    state->player_alive = 0;
                    state->game_over = 1;
                }
            }
        }
    }
}

void Update_Explosion_Lifecycle(Game1State* state, uint32_t now) {
    if (state->explosion_active &&
        (now - state->explosion_start_tick >= EXPLOSION_DURATION_MS)) {

        state->explosion_active = 0;
        state->explosion_count = 0;
    }
}

void Game1_SpawnEnemy(Game1State* state) {
    for (int e = 0; e < MAX_ENEMIES; e++) {
        if (state->enemy_active[e])
            continue;
        int tries = 20;  
        while (tries--) {
            int r = rand() % (MAP_ROWS - 2) + 1;
            int c = rand() % (MAP_COLS - 2) + 1;
            if (state->map[r][c] != TILE_EMPTY)
                continue;
            if (r == state->player_row && c == state->player_col)
                continue;
            if (abs(r - state->player_row) + abs(c - state->player_col) < 2)
                continue;
            int safe = 1;
            for (int i = 0; i < state->explosion_count; i++) {
                if (r == state->explosion_rows[i] &&
                    c == state->explosion_cols[i]) {
                    safe = 0;
                    break;
                }
            }
            if (!safe)
                continue;
            int occupied = 0;
            for (int j = 0; j < MAX_ENEMIES; j++) {
                if (state->enemy_active[j] &&
                    state->enemy_row[j] == r &&
                    state->enemy_col[j] == c) {
                    occupied = 1;
                    break;
                }
            }
            if (occupied)
                continue;
            state->enemy_active[e] = 1;
            state->enemy_row[e] = r;
            state->enemy_col[e] = c;
            state->enemy_last_move_tick[e] = HAL_GetTick();
            return;
        }
    }
}
