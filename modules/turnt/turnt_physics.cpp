#include "turnt_physics.h"
#include "scene/3d/physics_body.h"

static TurntPhysics* singleton = nullptr;

TurntPhysics::TurntPhysics()
{
    singleton = this;
}

TurntPhysics::~TurntPhysics()
{
    singleton = nullptr;
}

/*static*/ TurntPhysics* TurntPhysics::get_singleton()
{
    return singleton;
}

Ref<GroundDetectResult> TurntPhysics::check_player_on_ground(Object* in_player, const float in_down_dist)
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
                player->get_global_transform().origin = Vector3(
                    save_pos_first_collision.x,
                    save_pos_first_collision.y + (in_down_dist * 0.25f),
                    save_pos_first_collision.z
                );

                gdr->m_ground_normal = col.normal;
                gdr->m_is_on_ground = true;

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
    player->get_global_transform().origin = save_pos;
    return gdr;
}

bool TurntPhysics::normal_is_ground(const Vector3& in_normal)
{
    return in_normal.y >= 0.7f;
}

/*static*/ void TurntPhysics::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("check_player_on_ground", "in_player", "in_down_dist"), &TurntPhysics::check_player_on_ground);
    ClassDB::bind_method(D_METHOD("normal_is_ground", "in_normal"), &TurntPhysics::normal_is_ground);
}