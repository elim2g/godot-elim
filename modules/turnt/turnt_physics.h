#ifndef TURNT_PHYSICS_H
#define TURNT_PHYSICS_H

#include "core/reference.h"
#include "physics_body.h"

class GroundDetectResult : public Reference
{
    GDCLASS(GroundDetectResult, Reference);

public:
    GroundDetectResult(Vector3 in_ground_normal, bool in_is_on_ground)
        : m_ground_normal(in_ground_normal)
        , m_is_on_ground(in_is_on_ground) {}
    GroundDetectResult(const GroundDetectResult &gdr)
        : m_ground_normal(gdr.m_ground_normal)
        , m_is_on_ground(gdr.m_is_on_ground) {}

    Vector3 get_ground_normal() const { return m_ground_normal; }
    bool get_is_on_ground() const { return m_is_on_ground; }

public:
    Vector3 m_ground_normal;
    bool m_is_on_ground;

protected:
    static void _bind_methods()
    {
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "ground_normal"), "", "get_ground_normal");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_on_ground"), "", "get_is_on_ground");
    }
};



class TurntPhysics : public Reference
{
    GDCLASS(TurntPhysics, Reference);

public:
    TurntPhysics() = default;

    static GroundDetectResult check_player_on_ground(KinematicBody* in_player, const float in_down_dist);
    static bool normal_is_ground(const Vector3& in_normal);

protected:
    static void _bind_methods()
    {
        ClassDB::bind_method(D_METHOD("check_player_on_ground", "in_player"), &TurntPhysics::check_player_on_ground);
        ClassDB::bind_method(D_METHOD("normal_is_ground", "in_normal"), &TurntPhysics::normal_is_ground);
    }
};

#endif // TURNT_PHYSICS_H