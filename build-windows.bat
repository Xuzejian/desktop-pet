@echo off
setlocal
cd /d "%~dp0"
if not exist dist mkdir dist
where g++ >nul 2>nul
if errorlevel 1 (
  echo Install MinGW-w64 and add its bin folder to PATH, then run this script again.
  exit /b 1
)
pushd src
windres pet.rc -O coff -o ..\dist\pet-res.o
if errorlevel 1 goto failed
g++ -std=c++17 -O2 -Wall -Wextra -municode -mwindows -static -Wl,--strip-all main.cpp ..\dist\pet-res.o -o ..\dist\FluffyPet.exe -lgdiplus -lole32 -lshell32 -lcomdlg32 -luser32 -lgdi32
if errorlevel 1 goto failed
popd
echo Built: dist\FluffyPet.exe
exit /b 0
:failed
popd
exit /b 1
