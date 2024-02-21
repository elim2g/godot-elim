#ifndef TURNT_PHYSICS_H
#define TURNT_PHYSICS_H

#include "core/reference.h"

class GroundDetectResult : public Reference
{
    GDCLASS(GroundDetectResult, Reference);

public:
    GroundDetectResult()
        : ground_normal(Vector3(0.0f, 1.0f, 0.0f))
        , is_on_ground(false) {}
    GroundDetectResult(Vector3 in_ground_normal, bool in_is_on_ground)
        : ground_normal(in_ground_normal)
        , is_on_ground(in_is_on_ground) {}

    Vector3 get_ground_normal() const { return ground_normal; }
    bool get_is_on_ground() const { return is_on_ground; }

public:
    Vector3 ground_normal;
    bool is_on_ground;

protected:
    static void _bind_methods()
    {
        ClassDB::bind_method(D_METHOD("get_ground_normal"), &GroundDetectResult::get_ground_normal);
        ClassDB::bind_method(D_METHOD("get_is_on_ground"), &GroundDetectResult::get_is_on_ground);

        ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "ground_normal"), "", "get_ground_normal");
        ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_on_ground"), "", "get_is_on_ground");
    }
};



class _TurntPhysics : public Object
{
    GDCLASS(_TurntPhysics, Object);

public:
    _TurntPhysics();
    ~_TurntPhysics();

    static _TurntPhysics* singleton;
    static _TurntPhysics* get_singleton();

    Ref<GroundDetectResult> check_player_on_ground(Object* in_player, const float in_down_dist);
    bool normal_is_ground(const Vector3& in_normal);

protected:
    static void _bind_methods();
};

#endif // TURNT_PHYSICS_H