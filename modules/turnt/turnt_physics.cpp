#include "turnt_physics.h"

/*static*/ GroundDetectResult TurntPhysics::check_player_on_ground(KinematicBody* in_player, const float in_down_dist)
{
    constexpr int MAX_BOUNCE_CHECKS = 5;
    const Vector3 save_pos = in_player->get_global_transform().origin;
    Vector3 down_vel = Vector3(0.0f, -in_down_dist, 0.0f);
    Vector3 save_pos_first_collision;

    KinematicBody::Collision col;
    for (int i = 0; i < MAX_BOUNCE_CHECKS; ++i)
    {
        // Collided with something
        if (in_player->move_and_collide(down_vel, true, col))
        {
            // Store first instance of hitting somethinhg and stopping
            // We will use this as the final position if we detect ground
            // NOTE: This is mostly a hack to deal with colliding w/ multiple surfaces
            if (i == 0)
            {
                save_pos_first_collision = in_player->get_global_transform().origin;
            }

            // We have found ground :)
            if (normal_is_ground(col.normal))
            {
                GroundDetectResult gdr { col.normal, true };
                in_player->get_global_transform().origin = Vector3(
                    save_pos_first_collision.x,
                    save_pos_first_collision.y + (in_down_dist * 0.25f),
                    save_pos_first_collision.z
                );

                return gdr;
            }
            // Did not find ground - but we will continue bouncing to see if we're
            // actually colliding with ground among other surfaces
            else
            {
                down_vel = down_vel.slide(col.normal).normalized() * col.remainder.length();
            }
        }
        else
        {
            break;
        }
    }

    // If we make it this far we have not detected any ground :(
    in_player->get_global_transform().origin = save_pos;
    return GroundDetectResult {
        Vector3(0.0f, 1.0f, 0.0f),
        false
    };
}

/*static*/ bool TurntPhysics::normal_is_ground(const Vector3& in_normal)
{
    return in_normal.y >= 0.7f;
}