#ifndef TURNT_PHYSICS_H
#define TURNT_PHYSICS_H

#include "core/reference.h"

class GroundDetectResult : public Reference
{
    GDCLASS(GroundDetectResult, Reference);

public:
    GroundDetectResult()
        : m_ground_normal(Vector3(0.0f, 1.0f, 0.0f))
        , m_is_on_ground(false) {}
    GroundDetectResult(Vector3 in_ground_normal, bool in_is_on_ground)
        : m_ground_normal(in_ground_normal)
        , m_is_on_ground(in_is_on_ground) {}

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



class TurntPhysics : public Object
{
    GDCLASS(TurntPhysics, Object);

public:
    TurntPhysics();
    ~TurntPhysics();

    static TurntPhysics* get_singleton();

    Ref<GroundDetectResult> check_player_on_ground(Object* in_player, const float in_down_dist);
    bool normal_is_ground(const Vector3& in_normal);

protected:
    static void _bind_methods();
};

#endif // TURNT_PHYSICS_H