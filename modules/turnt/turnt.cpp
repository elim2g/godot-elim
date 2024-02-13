#include "turnt.h"

Turnt::Turnt()
    : m_count(0)
{

}

void Turnt::add(int value)
{
    m_count += value;
}

void Turnt::reset()
{
    m_count = 0;
}

int Turnt::get_total() const
{
    return m_count;
}

/*static*/ void Turnt::_bind_methods()
{
    ClassDB::bind_method(D_METHOD("add", "value"), &Turnt::add);
    ClassDB::bind_method(D_METHOD("reset"), &Turnt::reset);
    ClassDB::bind_method(D_METHOD("get_total"), &Turnt::get_total);
}