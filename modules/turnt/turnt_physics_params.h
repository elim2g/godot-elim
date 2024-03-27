#ifndef TURNT_PHYSICS_PARAMS_H
#define TURNT_PHYSICS_PARAMS_H

#include "core/object.h"
#include "turnt_globals.h"

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

    float m_step_height;
    float m_step_y_norm_range;

    float m_ground_accel;
    float m_ground_speed;
    float m_ground_accel_slick;
    float m_ground_speed_slick;
    float m_ground_detect_dist;
    float m_friction;
    float m_stop_speed;
    float m_crouch_accel;
    float m_crouch_speed;

    float m_slope_y_max;
    float m_slope_peel;
    float m_slope_slide_peel;

    bool m_cpm_air_control_enabled;
    float m_cpm_air_control_strength;
    float m_cpm_air_accel;
    float m_cpm_air_speed;
    float m_air_accel;
    float m_air_speed;

    float m_gravity;
    float m_jump_velocity;
    float m_jump_dj_time_sec;
    float m_jump_dj_xplier;

    bool m_crouch_slide_enabled;
    float m_crouch_slide_speed;
    float m_crouch_slide_accel;
    float m_crouch_slide_max_duration;
    float m_crouch_slide_transition_lenience;
    float m_crouch_slide_hangtime_req_sec;
    float m_crouch_slide_efficiency_scalar;
    float m_crouch_slide_ground_detect_dist;
    float m_crouch_slide_downramp_gravity_scale;
    float m_crouch_slide_upramp_gravity_scale;

    float m_skim_time_sec;

    bool m_ramp_slides;
    float m_ramp_slide_vel_req;
    float m_ramp_slide_slope_y_min;
    float m_ramp_slide_slope_y_max;

    bool m_pwup_haste_enabled;
    float m_pwup_haste_xplier;

    float m_wep_rl_cooldown;
    float m_wep_rl_velocity;
    float m_wep_rl_splash_min_dist;
    float m_wep_rl_splash_max_dist;
    float m_wep_rl_splash_min_impulse;
    float m_wep_rl_splash_max_impulse;
    float m_wep_projectile_initial_distance;

    bool m_prac_mode_enabled;
    bool m_prac_spawn_rl;

    float m_ground_check_min_speed;
    float m_step_y_min_height;
    float m_ramp_slide_vel_req_squared;

public:
    // Getters
    float get_step_height() const;
    float get_step_y_norm_range() const;

    float get_ground_accel() const;
    float get_ground_speed() const;
    float get_ground_accel_slick() const;
    float get_ground_speed_slick() const;
    float get_ground_detect_dist() const;
    float get_friction() const;
    float get_stop_speed() const;
    float get_crouch_accel() const;
    float get_crouch_speed() const;

    float get_slope_y_max() const;
    float get_slope_peel() const;
    float get_slope_slide_peel() const;

    bool get_cpm_air_control_enabled() const;
    float get_cpm_air_control_strength() const;
    float get_cpm_air_accel() const;
    float get_cpm_air_speed() const;
    float get_air_accel() const;
    float get_air_speed() const;

    float get_gravity() const;
    float get_jump_velocity() const;
    float get_jump_dj_time_sec() const;
    float get_jump_dj_xplier() const;

    bool get_crouch_slide_enabled() const;
    float get_crouch_slide_speed() const;
    float get_crouch_slide_accel() const;
    float get_crouch_slide_max_duration() const;
    float get_crouch_slide_transition_lenience() const;
    float get_crouch_slide_hangtime_req_sec() const;
    float get_crouch_slide_efficiency_scalar() const;
    float get_crouch_slide_ground_detect_dist() const;
    float get_crouch_slide_downramp_gravity_scale() const;
    float get_crouch_slide_upramp_gravity_scale() const;

    float get_skim_time_sec() const;

    bool get_ramp_slides() const;
    float get_ramp_slide_vel_req() const;
    float get_ramp_slide_slope_y_min() const;
    float get_ramp_slide_slope_y_max() const;

    bool get_pwup_haste_enabled() const;
    float get_pwup_haste_xplier() const;

    float get_wep_rl_cooldown() const;
    float get_wep_rl_velocity() const;
    float get_wep_rl_splash_min_dist() const;
    float get_wep_rl_splash_max_dist() const;
    float get_wep_rl_splash_min_impulse() const;
    float get_wep_rl_splash_max_impulse() const;
    float get_wep_projectile_initial_distance() const;

    bool get_prac_mode_enabled() const;
    bool get_prac_spawn_rl() const;

    float get_ground_check_min_speed() const;
    float get_step_y_min_height() const;
    float get_ramp_slide_vel_req_squared() const;

    // Setters
    void set_step_height(const float value);
    void set_step_y_norm_range(const float value);

    void set_ground_accel(const float value);
    void set_ground_speed(const float value);
    void set_ground_accel_slick(const float value);
    void set_ground_speed_slick(const float value);
    void set_ground_detect_dist(const float value);
    void set_friction(const float value);
    void set_stop_speed(const float value);
    void set_crouch_accel(const float value);
    void set_crouch_speed(const float value);

    void set_slope_y_max(const float value);
    void set_slope_peel(const float value);
    void set_slope_slide_peel(const float value);

    void set_cpm_air_control_enabled(const bool enabled);
    void set_cpm_air_control_strength(const float value);
    void set_cpm_air_accel(const float value);
    void set_cpm_air_speed(const float value);
    void set_air_accel(const float value);
    void set_air_speed(const float value);

    void set_gravity(const float value);
    void set_jump_velocity(const float value);
    void set_jump_dj_time_sec(const float value);
    void set_jump_dj_xplier(const float value);

    void set_crouch_slide_enabled(const bool enabled);
    void set_crouch_slide_speed(const float value);
    void set_crouch_slide_accel(const float value);
    void set_crouch_slide_max_duration(const float value);
    void set_crouch_slide_transition_lenience(const float value);
    void set_crouch_slide_hangtime_req_sec(const float value);
    void set_crouch_slide_efficiency_scalar(const float value);
    void set_crouch_slide_ground_detect_dist(const float value);
    void set_crouch_slide_downramp_gravity_scale(const float value);
    void set_crouch_slide_upramp_gravity_scale(const float value);

    void set_skim_time_sec(const float value);

    void set_ramp_slides(const bool enabled);
    void set_ramp_slide_vel_req(const float value);
    void set_ramp_slide_slope_y_min(const float value);
    void set_ramp_slide_slope_y_max(const float value);

    void set_pwup_haste_enabled(const bool enabled);
    void set_pwup_haste_xplier(const float value);

    void set_wep_rl_cooldown(const float value);
    void set_wep_rl_velocity(const float value);
    void set_wep_rl_splash_min_dist(const float value);
    void set_wep_rl_splash_max_dist(const float value);
    void set_wep_rl_splash_min_impulse(const float value);
    void set_wep_rl_splash_max_impulse(const float value);
    void set_wep_projectile_initial_distance(const float value);

    void set_prac_mode_enabled(const bool enabled);
    void set_prac_spawn_rl(const bool enabled);

    void set_ground_check_min_speed(const float value);
    void set_step_y_min_height(const float value);
    void set_ramp_slide_vel_req_squared(const float value);

public:
    bool normal_is_wall(const Vector3 &in_normal) const;
    bool normal_is_ground(const Vector3 &in_normal) const;
    bool normal_is_ceiling(const Vector3 &in_normal) const;
    float gd2tnt_float(const float in_value) const;
    Vector2 gd2tnt_vec2(const Vector2 &in_value) const;
    Vector3 gd2tnt_vec3(const Vector3 &in_value) const;
};

#endif // TURNT_PHYSICS_PARAMS_H
