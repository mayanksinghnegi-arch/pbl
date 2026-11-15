@echo off
REM Build (if needed) and start FoodBridge, then open http://localhost:8080
cd /d "%~dp0"
if not exist backend\foodbridge.exe (
  echo Compiling backend - first build takes about 30 seconds...
  g++ -std=c++17 -O2 -D_WIN32_WINNT=0x0A00 backend/main.cpp -o backend/foodbridge.exe -lws2_32
  if errorlevel 1 ( echo Build failed. Make sure g++ ^(MinGW-w64^) is installed and on PATH. & pause & exit /b 1 )
)
start "" http://localhost:8080
backend\foodbridge.exe %*
pause
