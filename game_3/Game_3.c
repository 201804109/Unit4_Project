#include "Game_3.h"
#include "InputHandler.h"
#include "Menu.h"
#include "LCD.h"
#include "PWM.h"
#include "Buzzer.h"
#include "Joystick.h"
#include "sprites.h"  // Sprite graphics for rendering
#include "stm32l4xx_hal.h"
#include <stdio.h>
#include <stdlib.h>

extern ST7789V2_cfg_t cfg0;
extern PWM_cfg_t pwm_cfg;      
extern Buzzer_cfg_t buzzer_cfg; 
extern Joystick_cfg_t joystick_cfg; 

// Global high score
static uint32_t high_score = 0;

// --- Game Constants ---
#define GAME1_FRAME_TIME_MS 30  
#define GROUND_Y            200 

// --- Dino Parameters ---
#define DINO_X              40  
#define DINO_WIDTH          20  
#define DINO_NORMAL_H       20  
#define DINO_DUCK_H         10  

// --- Obstacle Parameters (UPDATED) ---
#define OBS_TYPE_CACTUS       0   
#define OBS_TYPE_BIRD_SINGLE  1   // Single bird (can be jumped over)
#define OBS_TYPE_BIRD_DOUBLE  2   // Double birds (top + bottom, must duck)

#define CACTUS_WIDTH          15  
#define CACTUS_HEIGHT         30  

#define BIRD_WIDTH            20  
#define BIRD_HEIGHT           15  
#define BIRD_Y_SINGLE         170 // Lower or fixed height so jumping can clear it
#define BIRD_Y_DOUBLE_TOP     135 // Double birds: top bird (blocks jump space)
#define BIRD_Y_DOUBLE_BOT     170 // Double birds: bottom bird (blocks standing space)

// --- Physics Parameters ---
#define GRAVITY             1.5f
#define FAST_FALL_GRAVITY   4.0f 
#define JUMP_VELOCITY       -15.0f 

MenuState Game3_Run(void) {
    float dino_feet_y = GROUND_Y;   
    float velocity = 0.0f;
    uint8_t is_ducking = 0;
    
    int16_t obs_x = 240;            
    int16_t obs_speed = 6;
    uint8_t obs_type = OBS_TYPE_CACTUS; 
    
    uint32_t score = 0;
    uint8_t game_over = 0;
    Joystick_t local_joy_data; 
    
    buzzer_tone(&buzzer_cfg, 1000, 30);  
    HAL_Delay(100);  
    buzzer_off(&buzzer_cfg);  
    PWM_SetDuty(&pwm_cfg, 0);

    MenuState exit_state = MENU_STATE_HOME;  
    
    while (1) {
        uint32_t frame_start = HAL_GetTick();
        
        // ===== INPUT HANDLING =====
        Input_Read();
        Joystick_Read(&joystick_cfg, &local_joy_data); 
        UserInput joy_input = Joystick_GetInput(&local_joy_data);
        
        if (current_input.btn3_pressed) {
            PWM_SetDuty(&pwm_cfg, 0); 
            buzzer_off(&buzzer_cfg);
            exit_state = MENU_STATE_HOME;
            break;  
        }
        
        // ===== GAME LOGIC UPDATE =====
        if (!game_over) {
            uint8_t is_on_ground = (dino_feet_y >= GROUND_Y);
            
            // Ducking 
            if (joy_input.direction == S || joy_input.direction == SE || joy_input.direction == SW) {
                is_ducking = 1;
                if (!is_on_ground) velocity += FAST_FALL_GRAVITY; 
            } else {
                is_ducking = 0;
            }
            
            // Jumping 
            if ((joy_input.direction == N || joy_input.direction == NE || joy_input.direction == NW) 
                && is_on_ground && !is_ducking) {
                velocity = JUMP_VELOCITY;
                is_on_ground = 0;
                buzzer_tone(&buzzer_cfg, 1500, 20); 
            }
            
            if (!is_on_ground) {
                velocity += GRAVITY;
                dino_feet_y += velocity;
            }
            
            if (dino_feet_y >= GROUND_Y) {
                dino_feet_y = GROUND_Y;
                velocity = 0;
                buzzer_off(&buzzer_cfg); 
            }
            
            obs_x -= obs_speed;
            
            if (obs_x < -20) {
                obs_x = 240; 
                score++;
                if (score > high_score) high_score = score;
                
                // Randomize obstacle type (UPDATED LOGIC)
                int rand_val = rand() % 100;
                if (rand_val < 40) obs_type = OBS_TYPE_CACTUS;              
                else if (rand_val < 70) obs_type = OBS_TYPE_BIRD_SINGLE;    
                else obs_type = OBS_TYPE_BIRD_DOUBLE;                       
                
                if (score % 5 == 0 && obs_speed < 15) obs_speed++;
                
                uint8_t brightness = (score * 5 > 100) ? 100 : (score * 5);
                PWM_SetDuty(&pwm_cfg, brightness);
            }
            
            // ===== COLLISION DETECTION (UPDATED) =====
            uint8_t current_dino_h = is_ducking ? DINO_DUCK_H : DINO_NORMAL_H;
            
            int dino_left = DINO_X;
            int dino_right = DINO_X + DINO_WIDTH;
            int dino_top = (int)dino_feet_y - current_dino_h; 
            int dino_bottom = (int)dino_feet_y;
            
            int obs_left = obs_x;
            int obs_right = obs_x + (obs_type == OBS_TYPE_CACTUS ? CACTUS_WIDTH : BIRD_WIDTH);
            
            int obs_top, obs_bottom;
            if (obs_type == OBS_TYPE_CACTUS) {
                obs_top = GROUND_Y - CACTUS_HEIGHT;
                obs_bottom = GROUND_Y;
            } else if (obs_type == OBS_TYPE_BIRD_SINGLE) {
                obs_top = BIRD_Y_SINGLE;
                obs_bottom = BIRD_Y_SINGLE + BIRD_HEIGHT;
            } else { // OBS_TYPE_BIRD_DOUBLE (Creates a tall hit-box to force ducking)
                obs_top = BIRD_Y_DOUBLE_TOP;
                obs_bottom = BIRD_Y_DOUBLE_BOT + BIRD_HEIGHT;
            }
            
            if (dino_right > obs_left && dino_left < obs_right && 
                dino_bottom > obs_top && dino_top < obs_bottom) {
                game_over = 1; 
                PWM_SetDuty(&pwm_cfg, 100); 
                buzzer_tone(&buzzer_cfg, 400, 50); HAL_Delay(120);
                buzzer_tone(&buzzer_cfg, 300, 50); HAL_Delay(120);
                buzzer_tone(&buzzer_cfg, 200, 50); HAL_Delay(150);
                buzzer_off(&buzzer_cfg); 
            }
            
        } else {
            if (joy_input.direction == W || current_input.btn2_pressed) {
                game_over = 0;
                dino_feet_y = GROUND_Y;
                velocity = 0;
                obs_x = 240;
                obs_speed = 6;
                score = 0;
                is_ducking = 0;
                PWM_SetDuty(&pwm_cfg, 0);
            }
        }
        
        // ===== RENDERING =====
        LCD_Fill_Buffer(LCD_COLOUR_13); 
        LCD_Draw_Line(0, GROUND_Y, 240, GROUND_Y, LCD_COLOUR_0);
        
        if (!game_over) {
            uint8_t current_dino_h = is_ducking ? DINO_DUCK_H : DINO_NORMAL_H;
            uint16_t dino_draw_y = (uint16_t)(dino_feet_y - current_dino_h);
            
            if (is_ducking) {
                LCD_Draw_Sprite(DINO_X, dino_draw_y, DINO_DUCK_H, DINO_WIDTH, dino_duck_data[0]);
            } else if (dino_feet_y < GROUND_Y) {
                LCD_Draw_Sprite(DINO_X, dino_draw_y, DINO_NORMAL_H, DINO_WIDTH, dino_run_data[0]);
            } else {
                uint8_t run_frame = 1 + ((HAL_GetTick() / 100) % 2); 
                LCD_Draw_Sprite(DINO_X, dino_draw_y, DINO_NORMAL_H, DINO_WIDTH, dino_run_data[run_frame]);
            }
            
            // Render Obstacles (UPDATED)
            if (obs_type == OBS_TYPE_CACTUS) {
                LCD_Draw_Sprite(obs_x, GROUND_Y - CACTUS_HEIGHT, CACTUS_HEIGHT, CACTUS_WIDTH, cactus_data[0]);
            } else if (obs_type == OBS_TYPE_BIRD_SINGLE) {
                uint8_t bird_frame = (HAL_GetTick() / 150) % 2; 
                LCD_Draw_Sprite(obs_x, BIRD_Y_SINGLE, BIRD_HEIGHT, BIRD_WIDTH, bird_data[bird_frame]);
            } else if (obs_type == OBS_TYPE_BIRD_DOUBLE) {
                uint8_t bird_frame = (HAL_GetTick() / 150) % 2; 
                LCD_Draw_Sprite(obs_x, BIRD_Y_DOUBLE_TOP, BIRD_HEIGHT, BIRD_WIDTH, bird_data[bird_frame]); // Top bird
                LCD_Draw_Sprite(obs_x, BIRD_Y_DOUBLE_BOT, BIRD_HEIGHT, BIRD_WIDTH, bird_data[bird_frame]); // Bottom bird
            }
            
            char hi_str[32]; sprintf(hi_str, "HI: %lu", high_score);
            LCD_printString(hi_str, 10, 10, LCD_COLOUR_0, 2);
            char score_str[32]; sprintf(score_str, "SCR: %lu", score);
            LCD_printString(score_str, 130, 10, LCD_COLOUR_0, 2);
            
        } else {
            LCD_printString("GAME OVER", 50, 80, LCD_COLOUR_2, 3);
            char final_score[32]; sprintf(final_score, "Score: %lu", score);
            LCD_printString(final_score, 80, 130, LCD_COLOUR_0, 2);
            char best_score[32]; sprintf(best_score, "Best:  %lu", high_score);
            LCD_printString(best_score, 80, 155, LCD_COLOUR_0, 2);
            LCD_printString("LEFT to Restart", 30, 190, LCD_COLOUR_0, 2);
            LCD_printString("BT3 to Menu", 54, 215, LCD_COLOUR_0, 2);
        }
        
        LCD_Refresh(&cfg0);
        
        uint32_t frame_time = HAL_GetTick() - frame_start;
        if (frame_time < GAME1_FRAME_TIME_MS) HAL_Delay(GAME1_FRAME_TIME_MS - frame_time);
    }
    return exit_state; 
}
