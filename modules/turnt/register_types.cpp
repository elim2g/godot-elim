#include "register_types.h"

#include "core/class_db.h"
#include "turnt.h"
#include "turnt_physics.h"

void register_turnt_types()
{
    ClassDB::register_class<Turnt>();
    ClassDB::register_class<TurntPhysics>();
    ClassDB::register_class<GroundDetectResult>();
}

void unregister_turnt_types()
{
    // Nottin 2 do m8
}