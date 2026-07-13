#pragma once
void servo_manager_init(void);
void servo_actuate_latch(void);
void servo_step_manual(int step_degrees);

void servo_set_manual(int target_angle);

void servo_trigger_unlock_sequence(void);
