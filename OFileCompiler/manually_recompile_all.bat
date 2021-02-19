REM go to the raw shaders directory
cd ..\raw_assets\shaders

REM iterate over all vertex, fragment and compute shaders
for %%i in (*.vert *.frag *.comp) do (
      ..\..\Dependencies\bin\glslangValidator.exe -V "%%~fi" -o "..\..\_assets\shaders\%%~nxi.spv"
)

REM go to the raw meshes folder
cd ..\models

REM iterate over all .obj meshes
for %%j in (*.obj) do (
       REM compile the .obj file into a .o
       ..\..\OFileCompiler\build\OFileCompiler.exe -src "%%~fj"  -dst "..\..\_assets\models\%%~nj.o"
)

pause