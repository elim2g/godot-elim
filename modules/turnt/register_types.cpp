#include "register_types.h"

#include "core/class_db.h"
#include "core/engine.h"
#include "turnt.h"
#include "turnt_physics.h"

static TurntPhysics* turnt_physics = nullptr;

void register_turnt_types()
{
    ClassDB::register_class<Turnt>();
    ClassDB::register_class<TurntPhysics>();
    ClassDB::register_class<GroundDetectResult>();

    turnt_physics = memnew(TurntPhysics());
}

void unregister_turnt_types()
{
    memdelete(turnt_physics);
}

void register_turnt_singletons()
{
	Engine::get_singleton()->add_singleton(Engine::Singleton("TurntPhysics", TurntPhysics::get_singleton()));
}