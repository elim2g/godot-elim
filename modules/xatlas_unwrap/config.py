def can_build(env, platform):
    # <ELIM> Upstream builds the xatlas unwrapper only in editor builds. TURNT's
    # in-game map editor unwraps UV2 at runtime ahead of a lightmap bake
    # (ArrayMesh::lightmap_unwrap), so template builds need it alongside lightmapper_rd.
    # return env.editor_build
    return not env["disable_3d"]
    # </ELIM>


def configure(env):
    pass
