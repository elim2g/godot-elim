def can_build(env, platform):
    # <ELIM> Upstream builds the GPU lightmapper only in editor builds. TURNT's
    # in-game map editor bakes lightmaps at runtime, so template builds need it too.
    return not env["disable_3d"] # </ELIM>


def configure(env):
    pass
