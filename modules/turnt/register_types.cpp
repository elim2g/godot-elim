#include "register_types.h"

#include "core/class_db.h"
#include "core/engine.h"
#include "turnt.h"
#include "turnt_physics.h"
#include "netcode/turnt_chat_server.h"
#include "netcode/turnt_chat_client.h"



static _TurntPhysics* turnt_physics = nullptr;



void register_turnt_types()
{
    turnt_physics = memnew(_TurntPhysics);

    ClassDB::register_class<Turnt>();
    ClassDB::register_class<GroundDetectResult>();
    ClassDB::register_class<_TurntPhysics>();
    ClassDB::register_class<TurntChatServer>();
    ClassDB::register_class<TurntChatClient>();

	Engine::get_singleton()->add_singleton(Engine::Singleton("TurntPhysics", _TurntPhysics::get_singleton()));
}



void unregister_turnt_types()
{
    memdelete(turnt_physics);
}