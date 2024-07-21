scons -j24 platform=windows bits=64 target=release_debug

cd bin

C:\msys64\ucrt64\bin\strip.exe godot.windows.opt.tools.64.exe

del Godot.exe

rename godot.windows.opt.tools.64.exe Godot.exe

del *.exp

del *.lib

del *.pdb

cd ..