@echo off
REM PathFinder-C - Windows build (needs MinGW-w64 gcc on PATH)
gcc -std=c11 -O2 -Wall -Wextra -static -o pathfinder.exe src\strbuf.c src\graph.c src\ds.c src\algorithms.c src\server.c src\main.c -lws2_32 -lshell32
if errorlevel 1 (
  echo Build failed. Install MinGW-w64 and make sure gcc is on your PATH.
  exit /b 1
)
gcc -std=c11 -O2 -Wall -Wextra -Isrc -o test_algorithms.exe tests\test_algorithms.c src\strbuf.c src\graph.c src\ds.c src\algorithms.c
echo Build OK. Double-click pathfinder.exe (or run it here) - it opens http://localhost:8080
