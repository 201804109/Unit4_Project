
#include "Game_2.h"
#include "Menu.h"
#include "InputHandler.h"

#include "LCD.h"
#include "Buzzer.h"
#include "Joystick.h"
#include "PWM.h"
#include "Utils.h"
#include "stm32l4xx_hal.h"

#include <stdio.h>
#include <string.h>


extern ADC_HandleTypeDef hadc1;
extern RNG_HandleTypeDef hrng;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim4;

extern ST7789V2_cfg_t  cfg0;          /* 共享 LCD     */
extern Buzzer_cfg_t    buzzer_cfg;    /* 共享 Buzzer  */


static Joystick_cfg_t joystick_cfg = {
    .adc           = &hadc1,
    .x_channel     = ADC_CHANNEL_1,
    .y_channel     = ADC_CHANNEL_2,
    .sampling_time = ADC_SAMPLETIME_47CYCLES_5,
    .center_x      = JOYSTICK_DEFAULT_CENTER_X,
    .center_y      = JOYSTICK_DEFAULT_CENTER_Y,
    .deadzone      = JOYSTICK_DEADZONE,
    .setup_done    = 0
};

static Joystick_t joystick_data;

static PWM_cfg_t pwm_cfg = {
    .htim         = &htim4,           /* PB6 / TIM4_CH1 → LED  */
    .channel      = TIM_CHANNEL_1,
    .tick_freq_hz = 1000000,
    .min_freq_hz  = 10,
    .max_freq_hz  = 50000,
    .setup_done   = 0
};


typedef enum {
    G2_TITLE,
    G2_PLAYING,
    G2_PAUSED,
    G2_GAMEOVER
} Game2State_t;

typedef struct {
    int16_t x;
    int16_t y;
} Player_t;

typedef struct {
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
    int16_t speed;
    uint8_t active;
} Obstacle_t;


static Game2State_t  game_state;
static Player_t      player;
static Obstacle_t    obstacles[OBS_MAX_COUNT];
static int16_t       lives;
static uint32_t      score;
static uint32_t      frame_count;
static int16_t       obs_speed;
static uint16_t      current_spawn_interval;

static uint32_t      last_difficulty_increase;
static uint32_t      buzzer_off_time;
static uint32_t      led_off_time;
static uint32_t      speedup_anim_start;
static uint32_t      pause_start_time;
static uint8_t       joystick_released;


static void     game_init(void);
static void     update_game(void);
static void     render_game(void);
static void     render_title(void);
static void     render_pause(void);
static void     render_gameover(void);

static void     check_collisions(void);
static void     spawn_obstacle(void);
static void     update_obstacles(void);

static void     play_startup_sound(void);
static void     play_hit_sound(void);
static void     play_score_sound(void);
static void     play_gameover_sound(void);

static void     draw_pixel_heart(int16_t x0, int16_t y0);
static void     draw_plane(int16_t x, int16_t y, uint8_t colour);
static uint8_t  joystick_is_centered(void);
static uint8_t  joystick_is_moved(void);


MenuState Game2_Run(void)
{
    Joystick_Init(&joystick_cfg);
    Joystick_Calibrate(&joystick_cfg);
    PWM_Init(&pwm_cfg);

   
    game_state = G2_TITLE;
    LCD_Fill_Buffer(0);
    render_title();
    play_startup_sound();

    uint32_t  last_frame_time = HAL_GetTick();
    MenuState exit_state      = MENU_STATE_HOME;  

    
    while (1)
    {
        
        if (buzzer_off_time > 0 && HAL_GetTick() >= buzzer_off_time) {
            buzzer_off(&buzzer_cfg);
            buzzer_off_time = 0;
        }
        if (led_off_time > 0 && HAL_GetTick() >= led_off_time) {
            PWM_Off(&pwm_cfg);
            led_off_time = 0;
        }

      
        uint32_t current_time = HAL_GetTick();
        if (current_time - last_frame_time < FRAME_TIME_MS) {
            continue;
        }
        last_frame_time = current_time;

        
        Input_Read();
        Joystick_Read(&joystick_cfg, &joystick_data);

        if (joystick_is_centered()) {
            joystick_released = 1;
        }

      
        if (current_input.btn3_pressed) {
            exit_state = MENU_STATE_HOME;
            break;
        }

        switch (game_state)
        {
            case G2_TITLE:
                if (joystick_released && joystick_is_moved()) {
                    joystick_released = 0;
                    game_init();
                    game_state = G2_PLAYING;
                }
                break;

            case G2_PLAYING:
                /* BT2 切换暂停（替代原 PC2 直接读取） */
                if (current_input.btn2_pressed && joystick_released) {
                    joystick_released = 0;
                    pause_start_time  = HAL_GetTick();
                    game_state        = G2_PAUSED;
                    break;
                }

                frame_count++;
                update_game();

                if (game_state == G2_PLAYING) {
                    render_game();
                }
                break;

            case G2_PAUSED:
                render_pause();

                if (current_input.btn2_pressed && joystick_released) {
                    joystick_released = 0;

                    
                    uint32_t pause_duration = HAL_GetTick() - pause_start_time;
                    last_difficulty_increase += pause_duration;
                    if (speedup_anim_start > 0) {
                        speedup_anim_start += pause_duration;
                    }
                    game_state = G2_PLAYING;
                }
                break;

            case G2_GAMEOVER:
                if (joystick_released && joystick_is_moved()) {
                    joystick_released = 0;
                    game_state        = G2_TITLE;
                    LCD_Fill_Buffer(0);
                    render_title();
                }
                break;

            default:
                break;
        }
    }

  
    buzzer_off(&buzzer_cfg);
    PWM_Off(&pwm_cfg);
    return exit_state;
}


static uint8_t joystick_is_centered(void)
{
    return (joystick_data.coord_mapped.x >= -0.2f &&
            joystick_data.coord_mapped.x <=  0.2f &&
            joystick_data.coord_mapped.y >= -0.2f &&
            joystick_data.coord_mapped.y <=  0.2f);
}

static uint8_t joystick_is_moved(void)
{
    return (joystick_data.coord_mapped.x >  0.5f ||
            joystick_data.coord_mapped.x < -0.5f ||
            joystick_data.coord_mapped.y >  0.5f ||
            joystick_data.coord_mapped.y < -0.5f);
}


static void game_init(void)
{
    player.x = (SCREEN_WIDTH - PLAYER_WIDTH) / 2;
    player.y = PLAYER_START_Y;

    for (int i = 0; i < OBS_MAX_COUNT; i++) {
        obstacles[i].active = 0;
        obstacles[i].x      = 0;
        obstacles[i].y      = 0;
        obstacles[i].width  = 0;
        obstacles[i].height = 0;
        obstacles[i].speed  = 0;
    }

    lives                  = LIVES_INIT;
    score                  = 0;
    frame_count            = 0;
    obs_speed              = OBS_SPEED_INIT;
    current_spawn_interval = OBS_SPAWN_INTERVAL;
    buzzer_off_time        = 0;
    led_off_time           = 0;
    speedup_anim_start     = 0;
    joystick_released      = 0;

    last_difficulty_increase = HAL_GetTick();
}


static void update_game(void)
{
   
    if (joystick_data.coord_mapped.x >  0.3f) player.x += PLAYER_SPEED;
    if (joystick_data.coord_mapped.x < -0.3f) player.x -= PLAYER_SPEED;

    if (player.x < 0)                          player.x = 0;
    if (player.x > SCREEN_WIDTH - PLAYER_WIDTH) player.x = SCREEN_WIDTH - PLAYER_WIDTH;

    if (HAL_GetTick() - last_difficulty_increase > DIFFICULTY_INTERVAL_MS) {
        obs_speed++;
        last_difficulty_increase = HAL_GetTick();
        speedup_anim_start       = HAL_GetTick();
    }

 
    if (frame_count % current_spawn_interval == 0) {
        spawn_obstacle();
    }

    update_obstacles();
    check_collisions();
}



static void spawn_obstacle(void)
{
    for (int i = 0; i < OBS_MAX_COUNT; i++) {
        if (!obstacles[i].active) {
            uint32_t rand_val;
            HAL_RNG_GenerateRandomNumber(&hrng, &rand_val);

            obstacles[i].width  = 24;
            obstacles[i].height = 24;
            obstacles[i].speed  = obs_speed + (rand_val % 2);
            obstacles[i].x      = rand_val % (SCREEN_WIDTH - 24);
            obstacles[i].y      = -24;
            obstacles[i].active = 1;
            return;
        }
    }
}


static void update_obstacles(void)
{
    for (int i = 0; i < OBS_MAX_COUNT; i++) {
        if (!obstacles[i].active) continue;

        obstacles[i].y += obstacles[i].speed;

        if (obstacles[i].y > SCREEN_HEIGHT) {
            obstacles[i].active = 0;
            score++;
            play_score_sound();
        }
    }
}


static void check_collisions(void)
{
    for (int i = 0; i < OBS_MAX_COUNT; i++) {
        if (!obstacles[i].active) continue;

        uint8_t overlap_x = (player.x < obstacles[i].x + obstacles[i].width) &&
                            (player.x + PLAYER_WIDTH > obstacles[i].x);
        uint8_t overlap_y = (player.y < obstacles[i].y + obstacles[i].height) &&
                            (player.y + PLAYER_HEIGHT > obstacles[i].y);

        if (overlap_x && overlap_y) {
            obstacles[i].active = 0;
            lives--;

            if (lives <= 0) {
                buzzer_off(&buzzer_cfg);
                buzzer_off_time = 0;
                play_gameover_sound();

              
                PWM_Set(&pwm_cfg, 1000, 100);
                led_off_time = HAL_GetTick() + 1000;

                joystick_released = 0;
                game_state        = G2_GAMEOVER;
                render_gameover();
                return;
            } else {
                play_hit_sound();

           
                PWM_Set(&pwm_cfg, 1000, 100);
                led_off_time = HAL_GetTick() + 300;
            }
        }
    }
}


static void render_game(void)
{
    LCD_Fill_Buffer(0);

   
    draw_plane(player.x, player.y, 3);

    
    for (int i = 0; i < OBS_MAX_COUNT; i++) {
        if (!obstacles[i].active) continue;

        int16_t r  = 12;
        int16_t bx = obstacles[i].x + r;
        int16_t by = obstacles[i].y + r;

        
        LCD_Draw_Circle(bx, by, r, 7, 1);

        
        LCD_Draw_Line(bx - 1, by - r, bx + 6, by - r - 10, 7);
        LCD_Draw_Line(bx,     by - r, bx + 7, by - r - 10, 7);
        LCD_Draw_Line(bx + 1, by - r, bx + 8, by - r - 10, 7);

        
        LCD_printString("*", bx + 6, by - r - 15, 2, 1);
    }

   
    char info[40];
    sprintf(info, "SCORE:%u  LIFE:%d", (unsigned int)score, (int)lives);
    LCD_printString(info, 5, 5, 1, 1);

    
    if (speedup_anim_start > 0) {
        uint32_t elapsed = HAL_GetTick() - speedup_anim_start;

        if (elapsed < SPEEDUP_DISPLAY_MS) {
            int16_t text_y = 220 - (int16_t)(120 * elapsed / SPEEDUP_DISPLAY_MS);
            LCD_printString("SPEED UP!", 30, text_y, 10, 3);
        } else {
            speedup_anim_start = 0;
        }
    }

    
    LCD_printString("BT3:Menu", 175, 5, 1, 1);

    LCD_Refresh(&cfg0);
}


static void render_pause(void)
{
    LCD_printString("PAUSED",       66, 90,  1, 3);
    LCD_printString("BT2: Resume",  84, 140, 1, 1);
    LCD_printString("BT3: Menu",    93, 155, 1, 1);
    LCD_Refresh(&cfg0);
}


static void render_gameover(void)
{
    LCD_Fill_Buffer(0);

    LCD_printString("GAME OVER", 12, 80, 2, 4);

    char final_score[24];
    sprintf(final_score, "FINAL SCORE: %lu", (unsigned long)score);
    LCD_printString(final_score, 24, 140, 1, 2);

    LCD_printString("PUSH JOYSTICK", 50, 195, 1, 1);
    LCD_printString("TO RESTART",    65, 210, 1, 1);
    LCD_printString("BT3: MENU",     75, 225, 1, 1);

    LCD_Refresh(&cfg0);
}


static void draw_pixel_heart(int16_t x0, int16_t y0)
{
    const uint8_t heart_map[11][13] = {
        {0,0,2,2,2,0,0,0,2,2,2,0,0},
        {0,2,1,1,1,2,0,2,1,1,1,2,0},
        {2,1,1,1,1,1,2,1,1,1,1,1,2},
        {2,1,1,1,1,1,1,1,1,1,1,1,2},
        {2,1,1,1,1,1,1,1,1,1,1,1,2},
        {0,2,1,1,1,1,1,1,1,1,1,2,0},
        {0,0,2,1,1,1,1,1,1,1,2,0,0},
        {0,0,0,2,1,1,1,1,1,2,0,0,0},
        {0,0,0,0,2,1,1,1,2,0,0,0,0},
        {0,0,0,0,0,2,1,2,0,0,0,0,0},
        {0,0,0,0,0,0,2,0,0,0,0,0,0}
    };

    int scale = 3;
    for (int r = 0; r < 11; r++) {
        for (int c = 0; c < 13; c++) {
            if (heart_map[r][c] == 1) {
                LCD_Draw_Rect(x0 + c * scale, y0 + r * scale, scale, scale, 2, 1);
            } else if (heart_map[r][c] == 2) {
                LCD_Draw_Rect(x0 + c * scale, y0 + r * scale, scale, scale, 0, 1);
            }
        }
    }
}


static void render_title(void)
{
    LCD_Draw_Rect(0,   0, 240, 120, 8, 1);  /* 上：紫 */
    LCD_Draw_Rect(0, 120, 240, 120, 3, 1);  /* 下：绿 */

    draw_pixel_heart(15,  15);
    draw_pixel_heart(75,  15);
    draw_pixel_heart(135, 15);
    draw_pixel_heart(195, 15);

    LCD_printString("ULTIMATE", 35,  75, 3, 4);
    LCD_printString("FLIGHT",   50, 140, 8, 4);

    LCD_printString("PUSH JOYSTICK", 10, 205, 8, 1);
    LCD_printString("PUSH JOYSTICK", 11, 205, 8, 1);
    LCD_printString("TO START",      10, 220, 8, 1);
    LCD_printString("TO START",      11, 220, 8, 1);

    LCD_Draw_Circle(150, 215, 18, 10, 1);
    LCD_printString("$", 144, 207, 0, 2);

    LCD_Draw_Circle(205, 215, 18, 7, 1);
    LCD_Draw_Line(204, 197, 214, 187, 7);
    LCD_Draw_Line(205, 197, 215, 187, 7);
    LCD_Draw_Line(206, 197, 216, 187, 7);
    LCD_printString("*", 213, 182, 2, 2);

    LCD_Refresh(&cfg0);
}


static void draw_plane(int16_t x, int16_t y, uint8_t colour)
{
    LCD_Draw_Line(x,     y + 6,  x + 15, y + 6,  colour);
    LCD_Draw_Line(x,     y + 7,  x + 15, y + 7,  colour);
    LCD_Draw_Line(x + 7, y,      x + 7,  y + 13, colour);
    LCD_Draw_Line(x + 8, y,      x + 8,  y + 13, colour);
    LCD_Draw_Line(x + 4, y + 12, x + 11, y + 12, colour);
}


static void play_startup_sound(void)
{
    buzzer_tone(&buzzer_cfg, 1000, 50);
    buzzer_off_time = HAL_GetTick() + 200;
}

static void play_score_sound(void)
{
    buzzer_tone(&buzzer_cfg, 1500, 50);
    buzzer_off_time = HAL_GetTick() + 50;
}

static void play_hit_sound(void)
{
    buzzer_tone(&buzzer_cfg, 400, 50);
    buzzer_off_time = HAL_GetTick() + 300;
}

static void play_gameover_sound(void)
{
    buzzer_tone(&buzzer_cfg, 200, 50);
    buzzer_off_time = HAL_GetTick() + 1000;
}
