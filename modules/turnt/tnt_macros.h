#ifndef TNTMACROS_H
#define TNTMACROS_H



#define TNT_CLASS_NAME MyClass
#define XSTR(s) STR(s)
#define STR(s) #s



// Creating properties (member variable, getter and setter functions)
#define TNTPROP_G(TYPE,NAME) TYPE m_ ## NAME; \
                             TYPE get_ ## NAME() const { return m_ ## NAME; }

#define TNTPROP_GS(TYPE,NAME) TNTPROP_G(TYPE,NAME); \
                            void set_ ## NAME(const TYPE value) { m_ ## NAME = value; }



// Binding property functions so they're accessible from GDScript (getter and setter functions)
#define TNTBIND_G(NAME) ClassDB::bind_method(D_METHOD("get_" #NAME), & ##TNT_CLASS_NAME::get_ ## NAME)

#define TNTBIND_GS(NAME) TNTBIND_G(NAME); \
                        ClassDB::bind_method(D_METHOD("set_" #NAME), & ##TNT_CLASS_NAME::set_ ## NAME)



// Adding property names + their getter/setter funcs so the property is accessible via GDScript as if it were a native class
#define TNTADD_G(TYPE,NAME) TNTBIND_G(NAME); \
                        ADD_PROPERTY(PropertyInfo(Variant:: ##TYPE, #NAME), "", "get_" #NAME)

#define TNTADD_GS(TYPE,NAME) TNTBIND_GS(NAME); \
                        ADD_PROPERTY(PropertyInfo(Variant:: ##TYPE, #NAME), "set_" #NAME, "get_" #NAME)



// Basic logging with [class_name] prefix
#define TNT_LOG(msg, ...) OS::get_singleton()->print("[" XSTR(TNT_CLASS_NAME) "] " msg "\n", __VA_ARGS__)
#define TNT_LOGERR(msg, ...) OS::get_singleton()->printerr("[" XSTR(TNT_CLASS_NAME) "] " msg "\n", __VA_ARGS__)



#endif // TNTMACROS_H