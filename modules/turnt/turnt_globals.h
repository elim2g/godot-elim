#ifndef TURNT_GLOBALS_H
#define TURNT_GLOBALS_H

#include "core/object.h"

class _TurntGlobals : public Object
{
    GDCLASS(_TurntGlobals, Object);

public:
    static _TurntGlobals* singleton;
    static _TurntGlobals* get_singleton();

	_TurntGlobals();
	~_TurntGlobals();

    const int TICKRATE_INT = 125;
    const float TICKRATE_F = 125.0f;
    const int ONE_FRAME_MS_INT = 8;
    const float ONE_FRAME_MS_F = 8.0f;

    enum E_PHYSICS_PRESET
    {
        TPM,
        CPM,
        VQ3,
        VNT
    };

public:
	int get_tickrate_int() const;
	float get_tickrate_f() const;
	int get_one_frame_ms_int() const;
	float get_one_frame_ms_f() const;
	String physics_preset_as_str(const E_PHYSICS_PRESET in_preset) const;

protected:
    static void _bind_methods();
};

VARIANT_ENUM_CAST(_TurntGlobals::E_PHYSICS_PRESET);

#endif // TURNT_GLOBALS_H
