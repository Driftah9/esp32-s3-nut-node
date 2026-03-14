@echo off
set IDF_PATH=C:\Espressif\frameworks\esp-idf-v5.3.1
set IDF_TOOLS_PATH=C:\Espressif
set PYTHONNOUSERSITE=True
set PYTHONPATH=
set PYTHONHOME=
set "PATH=C:\Espressif\python_env\idf5.3_py3.11_env\Scripts;C:\Espressif\tools\idf-git\2.44.0\cmd;C:\Espressif\tools\cmake\3.24.0\bin;C:\Espressif\tools\ninja\1.11.1;C:\Espressif\tools\xtensa-esp-elf\esp-13.2.0_20240530\xtensa-esp-elf\bin;C:\Espressif\tools\riscv32-esp-elf\esp-13.2.0_20240530\riscv32-esp-elf\bin;%PATH%"
cd /d D:\Users\Stryder\Documents\Claude\Projects\esp32-s3-nut-node\src\current
echo Starting build...
C:\Espressif\python_env\idf5.3_py3.11_env\Scripts\python.exe C:\Espressif\frameworks\esp-idf-v5.3.1\tools\idf.py build
echo BUILD_EXIT:%ERRORLEVEL%
