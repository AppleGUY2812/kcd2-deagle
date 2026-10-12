@echo off
rem Render an ActorX .psk with its base-colour texture from 3 sides in headless Blender (no game involved).
rem usage: render.bat <mesh.psk> <basecolour.png> <out_prefix>   -> <out_prefix>_side.png / _front.png / _top.png
"E:\Tools\blender-4.3.2-windows-x64\blender.exe" -b --factory-startup -P "%~dp0psk_render.py" -- "%~1" "%~2" "%~3"
