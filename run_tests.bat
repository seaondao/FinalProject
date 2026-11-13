@echo off
REM Usage: run_tests.bat  (run from the folder with zip301.exe, unzip301.exe,
REM the reference Zip301.exe / Unzip301.exe renamed to ref_zip301.exe / ref_unzip301.exe,
REM and the sample .txt files)
setlocal enabledelayedexpansion
set FILES=palindrome gulliver jacob5 dc bom kjvbible canon minilog smallog log biglog

for %%F in (%FILES%) do (
  if exist %%F.txt (
    echo ===== %%F =====
    REM 1) ours -> ours
    zip301.exe %%F.txt
    unzip301.exe %%F.zip301
    fc /b %%F.txt %%F2.txt >nul && (echo   ours-^>ours   OK) || (echo   ours-^>ours   FAIL)
    REM 2) ours -> reference unzip
    del %%F2.txt
    ref_unzip301.exe %%F.zip301
    fc /b %%F.txt %%F2.txt >nul && (echo   ours-^>ref    OK) || (echo   ours-^>ref    FAIL)
    REM 3) reference zip -> ours
    del %%F2.txt
    ref_zip301.exe %%F.txt
    unzip301.exe %%F.zip301
    fc /b %%F.txt %%F2.txt >nul && (echo   ref-^>ours    OK) || (echo   ref-^>ours    FAIL)
    del %%F2.txt
  )
)

echo.
echo ===== timing biglog (ours) =====
powershell -NoProfile -Command "foreach($i in 1..3){ $z=(Measure-Command { .\zip301.exe biglog.txt }).TotalMilliseconds; $u=(Measure-Command { .\unzip301.exe biglog.zip301 }).TotalMilliseconds; Write-Host ('zip ' + [int]$z + ' ms   unzip ' + [int]$u + ' ms') }"
echo ===== timing biglog (reference) =====
powershell -NoProfile -Command "foreach($i in 1..3){ $z=(Measure-Command { .\ref_zip301.exe biglog.txt }).TotalMilliseconds; $u=(Measure-Command { .\ref_unzip301.exe biglog.zip301 }).TotalMilliseconds; Write-Host ('zip ' + [int]$z + ' ms   unzip ' + [int]$u + ' ms') }"
