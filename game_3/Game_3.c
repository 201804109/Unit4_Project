#include "Game_1.h"
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

// Global high score (retained during runtime until power off)
static uint32_t high_score = 0;

// --- Game Constants ---
#define GAME1_FRAME_TIME_MS 30  
#define GROUND_Y            200 

// --- Dino Parameters ---
#define DINO_X              40  
#define DINO_WIDTH          20  
#define DINO_NORMAL_H       20  
#define DINO_DUCK_H         10  

// --- Obstacle Parameters ---
#define OBS_TYPE_CACTUS     0   
#define OBS_TYPE_BIRD       1   

#define CACTUS_WIDTH        15  
#define CACTUS_HEIGHT       30  

#define BIRD_WIDTH          20  
#define BIRD_HEIGHT         15  
#define BIRD_Y              170 

// --- Physics Parameters ---
#define GRAVITY             1.5f
#define FAST_FALL_GRAVITY   4.0f 
#define JUMP_VELOCITY       -15.0f 

MenuState Game3_Run(void) {
    float dino_feet_y = GROUND_Y;   // Vertical position of Dino's feet
    float velocity = 0.0f;
    uint8_t is_ducking = 0;
    
    int16_t obs_x = 240;            // Obstacle initial position (right side)
    int16_t obs_speed = 6;
    uint8_t obs_type = OBS_TYPE_CACTUS; 
    
    uint32_t score = 0;
    uint8_t game_over = 0;
    
    Joystick_t local_joy_data; 
    
    // Startup sound effect
    buzzer_tone(&buzzer_cfg, 1000, 30);  
    HAL_Delay(100);  
    buzzer_off(&buzzer_cfg);  
    
    // Turn off LED initially
    PWM_SetDuty(&pwm_cfg, 0);

    MenuState exit_state = MENU_STATE_HOME;  
    
    while (1) {
        uint32_t frame_start = HAL_GetTick();
        
        // ===== INPUT HANDLING =====
        Input_Read();
        Joystick_Read(&joystick_cfg, &local_joy_data); 
        UserInput joy_input = Joystick_GetInput(&local_joy_data);
        
        // BTN3: Exit to main menu
        if (current_input.btn3_pressed) {
            PWM_SetDuty(&pwm_cfg, 0); 
            buzzer_off(&buzzer_cfg);
            exit_state = MENU_STATE_HOME;
            break;  
        }
        
        // ===== GAME LOGIC UPDATE =====
        if (!game_over) {
            uint8_t is_on_ground = (dino_feet_y >= GROUND_Y);
            
            // Ducking (Joystick Down)
            if (joy_input.direction == S || joy_input.direction == SE || joy_input.direction == SW) {
                is_ducking = 1;
                if (!is_on_ground) velocity += FAST_FALL_GRAVITY; // Accelerate falling
            } else {
                is_ducking = 0;
            }
            
            // Jumping (Joystick Up)
            if ((joy_input.direction == N || joy_input.direction == NE || joy_input.direction == NW) 
                && is_on_ground && !is_ducking) {
                
                velocity = JUMP_VELOCITY;
                is_on_ground = 0;
                
                // Jump sound
                buzzer_tone(&buzzer_cfg, 1500, 20); 
            }
            
            // Physics integration (gravity)
            if (!is_on_ground) {
                velocity += GRAVITY;
                dino_feet_y += velocity;
            }
            
            // Ground collision
            if (dino_feet_y >= GROUND_Y) {
                dino_feet_y = GROUND_Y;
                velocity = 0;
                buzzer_off(&buzzer_cfg); 
            }
            
            // Obstacle movement and scoring
            obs_x -= obs_speed;
            
            if (obs_x < -20) {
                obs_x = 240; 
                score++;
                
                // Update high score
                if (score > high_score) {
                    high_score = score;
                }
                
                // Randomize obstacle type
                obs_type = (rand() % 10 < 5) ? OBS_TYPE_CACTUS : OBS_TYPE_BIRD;
                
                // Increase difficulty over time
                if (score % 5 == 0 && obs_speed < 15) obs_speed++;
                
                // LED brightness reflects score
                uint8_t brightness = (score * 5 > 100) ? 100 : (score * 5);
                PWM_SetDuty(&pwm_cfg, brightness);
            }
            
            // ===== COLLISION DETECTION =====
            uint8_t current_dino_h = is_ducking ? DINO_DUCK_H : DINO_NORMAL_H;
            
            int dino_left = DINO_X;
            int dino_right = DINO_X + DINO_WIDTH;
            int dino_top = (int)dino_feet_y - current_dino_h; 
            int dino_bottom = (int)dino_feet_y;
            
            int obs_left = obs_x;
            int obs_right = obs_x + (obs_type == OBS_TYPE_CACTUS ? CACTUS_WIDTH : BIRD_WIDTH);
            int obs_top = (obs_type == OBS_TYPE_CACTUS) ? (GROUND_Y - CACTUS_HEIGHT) : BIRD_Y;
            int obs_bottom = obs_top + (obs_type == OBS_TYPE_CACTUS ? CACTUS_HEIGHT : BIRD_HEIGHT);
            
            // Axis-Aligned Bounding Box (AABB) collision
            if (dino_right > obs_left && dino_left < obs_right && 
                dino_bottom > obs_top && dino_top < obs_bottom) {
                
                game_over = 1; 
                PWM_SetDuty(&pwm_cfg, 100); 
                
                // Death sound sequence
                buzzer_tone(&buzzer_cfg, 400, 50);
                HAL_Delay(120);
                buzzer_tone(&buzzer_cfg, 300, 50);
                HAL_Delay(120);
                buzzer_tone(&buzzer_cfg, 200, 50);
                HAL_Delay(150);
                buzzer_off(&buzzer_cfg); 
            }
            
        } else {
            // Restart condition: joystick left OR button 2
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
        LCD_Fill_Buffer(LCD_COLOUR_13); // Light grey background
        
        // Draw ground line
        LCD_Draw_Line(0, GROUND_Y, 240, GROUND_Y, LCD_COLOUR_0);
        
        if (!game_over) {
            
            // Draw Dino sprite
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
            
            // Draw obstacle sprite
            if (obs_type == OBS_TYPE_CACTUS) {
                LCD_Draw_Sprite(obs_x, GROUND_Y - CACTUS_HEIGHT, CACTUS_HEIGHT, CACTUS_WIDTH, cactus_data[0]);
            } else {
                uint8_t bird_frame = (HAL_GetTick() / 150) % 2; 
                LCD_Draw_Sprite(obs_x, BIRD_Y, BIRD_HEIGHT, BIRD_WIDTH, bird_data[bird_frame]);
            }
            
            // HUD display (top of screen)
            char hi_str[32];
            sprintf(hi_str, "HI: %lu", high_score);
            LCD_printString(hi_str, 10, 10, LCD_COLOUR_0, 2);
            
            char score_str[32];
            sprintf(score_str, "SCR: %lu", score);
            LCD_printString(score_str, 130, 10, LCD_COLOUR_0, 2);
            
        } else {
            // Game Over screen
            LCD_printString("GAME OVER", 50, 80, LCD_COLOUR_2, 3);
            
            char final_score[32];
            sprintf(final_score, "Score: %lu", score);
            LCD_printString(final_score, 80, 130, LCD_COLOUR_0, 2);
            
            char best_score[32];
            sprintf(best_score, "Best:  %lu", high_score);
            LCD_printString(best_score, 80, 155, LCD_COLOUR_0, 2);
            
            LCD_printString("LEFT to Restart", 30, 190, LCD_COLOUR_0, 2);
            LCD_printString("BT3 to Menu", 54, 215, LCD_COLOUR_0, 2);
        }
        
        LCD_Refresh(&cfg0);
        
        // ===== FRAME RATE CONTROL =====
        uint32_t frame_time = HAL_GetTick() - frame_start;
        if (frame_time < GAME1_FRAME_TIME_MS) {
            HAL_Delay(GAME1_FRAME_TIME_MS - frame_time);
        }
    }
    
    return exit_state; 
}
