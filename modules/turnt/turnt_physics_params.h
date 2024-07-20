#ifndef TURNT_PHYSICS_PARAMS_H
#define TURNT_PHYSICS_PARAMS_H

#include "core/object.h"
#include "turnt_globals.h"

#include "tnt_macros.h"
#ifdef TNT_CLASS_NAME
#undef TNT_CLASS_NAME
#endif // TNT_CLASS_NAME
#define TNT_CLASS_NAME TurntPhysicsParams

class TurntPhysicsParams : public Object
{
    GDCLASS(TurntPhysicsParams, Object);

public:
    TurntPhysicsParams();

    const float GD_TO_TNT_UPS_RATIO = 30.4f;
    const float TNT_TO_GD_RATIO = 1.0f / GD_TO_TNT_UPS_RATIO;
    const float PHYSICS_TICK_RATE = 125.0f;

    const int PHY_BODIES_MASK = (1 << 0);
    const int PHY_AREAS_MASK = (1 << 1);
    const int PHY_SLICK_MASK = (1 << 2);

public:
    _TurntGlobals::E_PHYSICS_PRESET m_physics_preset;

    TNTPROP_GS(float, step_height);
    TNTPROP_GS(float, step_y_norm_range);

    TNTPROP_GS(float, ground_accel);
    TNTPROP_GS(float, ground_speed);
    TNTPROP_GS(float, ground_accel_slick);
    TNTPROP_GS(float, ground_speed_slick);
    TNTPROP_GS(float, ground_detect_dist);
    TNTPROP_GS(float, friction);
    TNTPROP_GS(float, stop_speed);
    TNTPROP_GS(float, crouch_accel);
    TNTPROP_GS(float, crouch_speed);

    TNTPROP_GS(float, slope_y_max);
    TNTPROP_GS(float, slope_peel);
    TNTPROP_GS(float, slope_slide_peel);

    TNTPROP_GS(bool, cpm_air_control_enabled);
    TNTPROP_GS(float, cpm_air_control_strength);
    TNTPROP_GS(float, cpm_air_accel);
    TNTPROP_GS(float, cpm_air_speed);
    TNTPROP_GS(float, air_accel);
    TNTPROP_GS(float, air_speed);
                    
    TNTPROP_GS(float, gravity);
    TNTPROP_GS(float, jump_velocity);
    TNTPROP_GS(float, jump_dj_time_sec);
    TNTPROP_GS(float, jump_dj_xplier);

    TNTPROP_GS(bool, crouch_slide_enabled);
    TNTPROP_GS(float, crouch_slide_speed);
    TNTPROP_GS(float, crouch_slide_accel);
    TNTPROP_GS(float, crouch_slide_max_duration);
    TNTPROP_GS(float, crouch_slide_transition_lenience);
    TNTPROP_GS(float, crouch_slide_hangtime_req_sec);
    TNTPROP_GS(float, crouch_slide_efficiency_scalar);
    TNTPROP_GS(float, crouch_slide_ground_detect_dist);
    TNTPROP_GS(float, crouch_slide_downramp_gravity_scale);
    TNTPROP_GS(float, crouch_slide_upramp_gravity_scale);

    TNTPROP_GS(float, skim_time_sec);

    TNTPROP_GS(bool, ramp_slides);
    TNTPROP_GS(float, ramp_slide_vel_req);
    TNTPROP_GS(float, ramp_slide_slope_y_min);
    TNTPROP_GS(float, ramp_slide_slope_y_max);

    TNTPROP_GS(bool, pwup_haste_enabled);
    TNTPROP_GS(float, pwup_haste_xplier);

    TNTPROP_GS(float, wep_rl_cooldown);
    TNTPROP_GS(float, wep_rl_velocity);
    TNTPROP_GS(float, wep_rl_splash_min_dist);
    TNTPROP_GS(float, wep_rl_splash_max_dist);
    TNTPROP_GS(float, wep_rl_splash_min_impulse);
    TNTPROP_GS(float, wep_rl_splash_max_impulse);
    TNTPROP_GS(float, wep_projectile_initial_distance);

    TNTPROP_GS(bool, prac_mode_enabled);
    TNTPROP_GS(bool, prac_spawn_rl);

    TNTPROP_GS(float, ground_check_min_speed);
    TNTPROP_GS(float, step_y_min_height);
    TNTPROP_GS(float, ramp_slide_vel_req_squared);

public:
    bool normal_is_wall(const Vector3 &in_normal) const;
    bool normal_is_ground(const Vector3 &in_normal) const;
    bool normal_is_ceiling(const Vector3 &in_normal) const;
    float gd2tnt_float(const float in_value) const;
    Vector2 gd2tnt_vec2(const Vector2 &in_value) const;
    Vector3 gd2tnt_vec3(const Vector3 &in_value) const;

protected:
    static void _bind_methods()
    {
        TNTADD_GS(REAL, step_height);
        TNTADD_GS(REAL, step_y_norm_range);

        TNTADD_GS(REAL, ground_accel);
        TNTADD_GS(REAL, ground_speed);
        TNTADD_GS(REAL, ground_accel_slick);
        TNTADD_GS(REAL, ground_speed_slick);
        TNTADD_GS(REAL, ground_detect_dist);
        TNTADD_GS(REAL, friction);
        TNTADD_GS(REAL, stop_speed);
        TNTADD_GS(REAL, crouch_accel);
        TNTADD_GS(REAL, crouch_speed);

        TNTADD_GS(REAL, slope_y_max);
        TNTADD_GS(REAL, slope_peel);
        TNTADD_GS(REAL, slope_slide_peel);

        TNTADD_GS(BOOL, cpm_air_control_enabled);
        TNTADD_GS(REAL, cpm_air_control_strength);
        TNTADD_GS(REAL, cpm_air_accel);
        TNTADD_GS(REAL, cpm_air_speed);
        TNTADD_GS(REAL, air_accel);
        TNTADD_GS(REAL, air_speed);
           
        TNTADD_GS(REAL, gravity);
        TNTADD_GS(REAL, jump_velocity);
        TNTADD_GS(REAL, jump_dj_time_sec);
        TNTADD_GS(REAL, jump_dj_xplier);

        TNTADD_GS(BOOL, crouch_slide_enabled);
        TNTADD_GS(REAL, crouch_slide_speed);
        TNTADD_GS(REAL, crouch_slide_accel);
        TNTADD_GS(REAL, crouch_slide_max_duration);
        TNTADD_GS(REAL, crouch_slide_transition_lenience);
        TNTADD_GS(REAL, crouch_slide_hangtime_req_sec);
        TNTADD_GS(REAL, crouch_slide_efficiency_scalar);
        TNTADD_GS(REAL, crouch_slide_ground_detect_dist);
        TNTADD_GS(REAL, crouch_slide_downramp_gravity_scale);
        TNTADD_GS(REAL, crouch_slide_upramp_gravity_scale);

        TNTADD_GS(REAL, skim_time_sec);

        TNTADD_GS(BOOL, ramp_slides);
        TNTADD_GS(REAL, ramp_slide_vel_req);
        TNTADD_GS(REAL, ramp_slide_slope_y_min);
        TNTADD_GS(REAL, ramp_slide_slope_y_max);

        TNTADD_GS(BOOL, pwup_haste_enabled);
        TNTADD_GS(REAL, pwup_haste_xplier);

        TNTADD_GS(REAL, wep_rl_cooldown);
        TNTADD_GS(REAL, wep_rl_velocity);
        TNTADD_GS(REAL, wep_rl_splash_min_dist);
        TNTADD_GS(REAL, wep_rl_splash_max_dist);
        TNTADD_GS(REAL, wep_rl_splash_min_impulse);
        TNTADD_GS(REAL, wep_rl_splash_max_impulse);
        TNTADD_GS(REAL, wep_projectile_initial_distance);

        TNTADD_GS(BOOL, prac_mode_enabled);
        TNTADD_GS(BOOL, prac_spawn_rl);

        TNTADD_GS(REAL, ground_check_min_speed);
        TNTADD_GS(REAL, step_y_min_height);
        TNTADD_GS(REAL, ramp_slide_vel_req_squared);
    }
};

#endif // TURNT_PHYSICS_PARAMS_H
