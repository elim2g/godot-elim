#include "turnt_physics.h"
#include "scene/3d/physics_body.h"



/*static*/ _TurntPhysics* _TurntPhysics::singleton = nullptr;



_TurntPhysics::_TurntPhysics()
{
    singleton = this;
}



_TurntPhysics::~_TurntPhysics()
{
    singleton = nullptr;
}



/*static*/ _TurntPhysics* _TurntPhysics::get_singleton()
{
    return singleton;
}



Ref<GroundDetectResult> _TurntPhysics::check_player_on_ground(Object* in_player, const float in_down_dist)
{
    Ref<GroundDetectResult> gdr = memnew(GroundDetectResult());

    KinematicBody* player = cast_to<KinematicBody>(in_player);
    if (!player)
    {
        return gdr;
    }

    constexpr int MAX_BOUNCE_CHECKS = 5;
    const Vector3 save_pos = player->get_global_transform().origin;
    Vector3 down_vel = Vector3(0.0f, -in_down_dist, 0.0f);
    Vector3 save_pos_first_collision;


    KinematicBody::Collision col;
    for (int i = 0; i < MAX_BOUNCE_CHECKS; ++i)
    {
        // Collided with something
        if (player->move_and_collide(down_vel, true, col))
        {
            // Store first instance of hitting somethinhg and stopping
            // We will use this as the final position if we detect ground
            // NOTE: This is mostly a hack to deal with colliding w/ multiple surfaces
            if (i == 0)
            {
                save_pos_first_collision = player->get_global_transform().origin;
            }

            // We have found ground :)
            if (normal_is_ground(col.normal))
            {
                player->set_global_translation(Vector3(
                    save_pos_first_collision.x,
                    save_pos_first_collision.y + (in_down_dist * 0.25f),
                    save_pos_first_collision.z
                ));

                gdr->ground_normal = col.normal;
                gdr->is_on_ground = true;

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
    player->set_global_translation(save_pos);

    return gdr;
}



bool _TurntPhysics::normal_is_ground(const Vector3& in_normal)
{
    return in_normal.y >= 0.7f;
}



Vector3 _TurntPhysics::slide_keep_xz_velocity(const Vector3 &in_velocity, const Vector3 &in_normal)
{
    const Vector3 slide_velocity = in_velocity.slide(in_normal);
    float slide_xy_len = Vector3(slide_velocity.x, 0.0f, slide_velocity.y).length();

    if (!Math::is_zero_approx(slide_xy_len))
    {
        return Vector3(
            in_velocity.x,
            slide_velocity.y * (Vector3(in_velocity.x, 0.0f, in_velocity.z).length() / slide_xy_len),
            in_velocity.z
        );
    }

    return slide_velocity;
}



/*static*/ void _TurntPhysics::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("check_player_on_ground", "in_player", "in_down_dist"), &_TurntPhysics::check_player_on_ground);
    ClassDB::bind_method(D_METHOD("normal_is_ground", "in_normal"), &_TurntPhysics::normal_is_ground);
    ClassDB::bind_method(D_METHOD("slide_keep_xz_velocity", "in_velocity", "in_normal"), &_TurntPhysics::slide_keep_xz_velocity);
}