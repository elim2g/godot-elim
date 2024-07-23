#include "turnt_globals.h"



/*static*/ _TurntGlobals *_TurntGlobals::singleton = nullptr;



_TurntGlobals::_TurntGlobals()
{
	singleton = this;
}



_TurntGlobals::~_TurntGlobals()
{
	singleton = nullptr;
}



/*static*/ _TurntGlobals* _TurntGlobals::get_singleton()
{
	return singleton;
}



int _TurntGlobals::get_tickrate_int() const
{
	return TICKRATE_INT;
}



float _TurntGlobals::get_tickrate_f() const
{
	return TICKRATE_F;
}



int _TurntGlobals::get_one_frame_ms_int() const
{
	return ONE_FRAME_MS_INT;
}



float _TurntGlobals::get_one_frame_ms_f() const
{
	return ONE_FRAME_MS_F;
}



String _TurntGlobals::physics_preset_as_str(const E_PHYSICS_PRESET in_preset) const
{
	switch (in_preset)
	{
	case E_PHYSICS_PRESET::TPM:
		return "TPM";

	case E_PHYSICS_PRESET::CPM:
		return "CPM";

	case E_PHYSICS_PRESET::VQ3:
		return "VQ3";

	case E_PHYSICS_PRESET::VNT:
		return "VNT";

	default:
		return "UNKNOWN_PRESET";
	}
}



/*static*/ void _TurntGlobals::_bind_methods()
{
	BIND_ENUM_CONSTANT(E_PHYSICS_PRESET::TPM);
	BIND_ENUM_CONSTANT(E_PHYSICS_PRESET::CPM);
	BIND_ENUM_CONSTANT(E_PHYSICS_PRESET::VQ3);
	BIND_ENUM_CONSTANT(E_PHYSICS_PRESET::VNT);

    ClassDB::bind_method(D_METHOD("get_tickrate_int"), &_TurntGlobals::get_tickrate_int);
    ClassDB::bind_method(D_METHOD("get_tickrate_f"), &_TurntGlobals::get_tickrate_f);
    ClassDB::bind_method(D_METHOD("get_one_frame_ms_int"), &_TurntGlobals::get_one_frame_ms_int);
    ClassDB::bind_method(D_METHOD("get_one_frame_ms_f"), &_TurntGlobals::get_one_frame_ms_f);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "TICKRATE_INT"), "", "get_tickrate_int");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "TICKRATE_F"), "", "get_tickrate_f");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "ONE_FRAME_MS_INT"), "", "get_one_frame_ms_int");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "ONE_FRAME_MS_F"), "", "get_one_frame_ms_f");
}
