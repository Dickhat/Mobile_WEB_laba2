
@echo off
cd /d "%~dp0"

if not exist ".venv\Scripts\python.exe" (
    echo Python virtual environment not found.
    echo Run: py -3 -m venv .venv
    pause
    exit /b 1
)

if not exist "certs\server.crt" (
    echo HTTPS certificate not found.
    pause
    exit /b 1
)

if not exist "certs\server.key" (
    echo HTTPS private key not found.
    pause
    exit /b 1
)

set "TASKS_JWT_SECRET=6fd5f0bd4611fa1f94d46633dafbc82161627a612d865fea7ad7c536d0f40e0e"

".venv\Scripts\python.exe" -m uvicorn app:app --host 0.0.0.0 --port 8443 --ssl-keyfile certs/server.key --ssl-certfile certs/server.crt

pause
