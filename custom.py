# TURNT custom SCons options. SConstruct reads this file for every scons
# invocation in this directory: editor builds, export templates, server builds.

# OpenXR + WebXR + mobile_vr. TURNT is desktop-only; no VR runtime ships.
disable_xr = "yes"

# CSG runtime nodes. TURNT loads brush geometry from TrenchBroom .map files via
# libturnt; CSGShape3D is never instantiated anywhere in the project.
module_csg_enabled = "no"

# GridMap. TURNT does not use grid-based level authoring.
module_gridmap_enabled = "no"

# BC6H encoders. Both modules are editor-only by default, so an export template
# has no Image.COMPRESS_BPTC backend at all: the in-game map editor's bake hits
# ERR_UNAVAILABLE and ships an uncompressed RGBH atlas ~6x larger than an editor
# bake of the same map. betsy is the GPU encoder taken first (compress_with_gpu
# defaults true); cvtt is the CPU fallback.
betsy_export_templates = "yes"
cvtt_export_templates = "yes"
