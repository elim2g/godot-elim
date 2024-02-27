#ifndef TURNT_PHYSICS_PARAMS_H
#define TURNT_PHYSICS_PARAMS_H

#include "core/object.h"

class TurntPhysicsParams : public Object
{
    GDCLASS(TurntPhysicsParams, Object);

public:
    TurntPhysicsParams();

    const float GD_TO_TNT_UPS_RATIO = 30.4f;
    const float TNT_TO_GD_RATIO = 1.0f / GD_TO_TNT_UPS_RATIO;
    const float PHYSICS_TICK_RATE = 125.0f;

    const int PHY_BODIES_MASK = (1 << 0);
    const int PHY_AREAS_MASK = (1 << 1);
    const int PHY_SLICK_MASK = (1 << 2);
};

#endif // TURNT_PHYSICS_PARAMS_H
