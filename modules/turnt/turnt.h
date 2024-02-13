#ifndef TURNT_H
#define TURNT_H

#include "core/reference.h"

class Turnt : public Reference
{
    GDCLASS(Turnt, Reference);

public:
    Turnt();

    void add(int value);
    void reset();
    int get_total() const;

protected:
    static void _bind_methods();

private:
    int m_count;
};

#endif // TURNT_H