#ifndef TURNT_PHYSICS_H
#define TURNT_PHYSICS_H

#include "core/reference.h"

class GroundDetectResult : public Reference
{
    GDCLASS(GroundDetectResult, Reference);

public:
    GroundDetectResult(Vector3 in_ground_normal, bool in_is_on_ground)
        : m_ground_normal(in_ground_normal)
        , m_is_on_ground(in_is_on_ground) {}

    Vector3 get_ground_normal() const { return m_ground_normal; }
    void set_ground_normal(Vector3 in_ground_normal) { m_ground_normal = in_ground_normal; }
    bool get_is_on_ground() const { return m_is_on_ground; }
    void set_is_on_ground(bool in_is_on_ground) { m_is_on_ground = in_is_on_ground; }

public:
    Vector3 m_ground_normal;
    bool m_is_on_ground;

protected:
    static void _bind_methods()
    {
        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "ground_normal"), "set_ground_normal", "get_ground_normal");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_on_ground"), "set_is_on_ground", "get_is_on_ground");
    }
};



class TurntPhysics : public Reference
{
    GDCLASS(TurntPhysics, Reference);

public:
    TurntPhysics() = default;

protected:
    static void _bind_methods()
    {

    }
};

#endif // TURNT_PHYSICS_H