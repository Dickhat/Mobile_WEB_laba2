"""ЛР №2. Tasks REST API. For educational use inside a trusted LAN."""
from contextlib import asynccontextmanager
from datetime import date, datetime, timedelta, timezone
from email.utils import format_datetime
from hashlib import pbkdf2_hmac, sha256
from pathlib import Path
from typing import Literal
import hmac
import json
import os
import secrets
import sqlite3
import uuid

import jwt
from fastapi import FastAPI, Depends, HTTPException, Request, Response, Query
from fastapi.security import HTTPBearer, HTTPAuthorizationCredentials
from fastapi.exceptions import RequestValidationError
from fastapi.responses import FileResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field, field_validator
from starlette.middleware.base import BaseHTTPMiddleware

ROOT = Path(__file__).resolve().parent
DB_PATH = Path(os.environ.get("TASKS_DB_PATH", str(ROOT / "data" / "tasks.sqlite3")))
JWT_SECRET = os.getenv("TASKS_JWT_SECRET", "demo-only-change-this-very-long-secret-2026")
JWT_LIFETIME_H = 24


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="microseconds")


def database():
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    con = sqlite3.connect(str(DB_PATH), timeout=10)
    con.row_factory = sqlite3.Row
    con.execute("PRAGMA busy_timeout = 10000")
    return con


def encode_password(password: str) -> str:
    salt = secrets.token_bytes(16)
    digest = pbkdf2_hmac("sha256", password.encode(), salt, 310000)
    return f"{salt.hex()}:{digest.hex()}"


def verify_password(password: str, stored: str) -> bool:
    try:
        salt_hex, digest_hex = stored.split(":", 1)
        actual = pbkdf2_hmac("sha256", password.encode(), bytes.fromhex(salt_hex), 310000)
        return hmac.compare_digest(actual, bytes.fromhex(digest_hex))
    except (ValueError, TypeError):
        return False


def init_db():
    with database() as con:
        con.executescript("""
            CREATE TABLE IF NOT EXISTS users (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                username TEXT NOT NULL UNIQUE,
                password_hash TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS tasks (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                owner_id INTEGER NOT NULL REFERENCES users(id),
                client_uid TEXT NOT NULL,
                title TEXT NOT NULL,
                description TEXT NOT NULL DEFAULT '',
                status TEXT NOT NULL DEFAULT 'new',
                due_date TEXT,
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL,
                UNIQUE(owner_id, client_uid)
            );
            CREATE INDEX IF NOT EXISTS idx_tasks_owner ON tasks(owner_id);
        """)
        if os.getenv("TASKS_DEMO_USERS", "1") == "1":
            for username, password in (("student", "student123"), ("guest", "guest123")):
                con.execute("INSERT OR IGNORE INTO users(username,password_hash) VALUES(?,?)",
                            (username, encode_password(password)))


@asynccontextmanager
async def lifespan(app: FastAPI):
    init_db()
    yield


app = FastAPI(title="Aurora Tasks — Лабораторная работа №2", version="2.0.0",
              description="REST API v1 и v2 списка задач с JWT, HTTPS, ETag и кэшированием.",
              lifespan=lifespan)


@app.exception_handler(RequestValidationError)
async def invalid_request_handler(request: Request, exc: RequestValidationError):
    return JSONResponse(status_code=400, content={"detail": "Некорректные данные запроса", "errors":
                         [{"loc": [str(x) for x in e["loc"]], "msg": e["msg"]} for e in exc.errors()]})


@app.exception_handler(sqlite3.Error)
async def sqlite_error_handler(request: Request, exc: sqlite3.Error):
    return JSONResponse(status_code=500, content={"detail": "Ошибка базы данных"})


@app.exception_handler(Exception)
async def internal_error_handler(request: Request, exc: Exception):
    return JSONResponse(status_code=500, content={"detail": "Внутренняя ошибка сервера"})


class Login(BaseModel):
    username: str = Field(min_length=1)
    password: str = Field(min_length=1)


class Token(BaseModel):
    access_token: str
    token_type: str = "bearer"
    expires_in: int


class TaskInput(BaseModel):
    title: str = Field(min_length=1, max_length=120)
    description: str = Field(default="", max_length=1000)
    status: Literal["new", "in_progress", "done"] = "new"
    due_date: date | None = None

    @field_validator("title")
    @classmethod
    def title_not_spaces(cls, value: str):
        if not value.strip():
            raise ValueError("Заголовок не может быть пустым")
        return value.strip()


class TaskCreate(TaskInput):
    # Client-generated stable UUID makes repeated offline POST idempotent.
    client_uid: uuid.UUID | None = None


class TaskPatch(BaseModel):
    # PATCH distinguishes an absent field from an explicit null (due_date can be cleared).
    title: str | None = Field(default=None, min_length=1, max_length=120)
    description: str | None = Field(default=None, max_length=1000)
    status: Literal["new", "in_progress", "done"] | None = None
    due_date: date | None = None

    @field_validator("title")
    @classmethod
    def patch_title_not_spaces(cls, value: str | None):
        if value is not None and not value.strip():
            raise ValueError("Заголовок не может быть пустым")
        return value.strip() if value is not None else None


class SummaryOut(BaseModel):
    total: int
    new: int
    in_progress: int
    done: int
    overdue: int


class TaskOut(BaseModel):
    id: int
    client_uid: str
    title: str
    description: str
    status: str
    due_date: str | None
    created_at: str
    updated_at: str


# HTTPBearer registers the security scheme in OpenAPI, so Swagger UI
# displays Authorize and sends the Authorization header automatically.
bearer_scheme = HTTPBearer(auto_error=False)


async def current_user(
    credentials: HTTPAuthorizationCredentials | None = Depends(bearer_scheme),
) -> int:
    if credentials is None:
        raise HTTPException(status_code=401, detail="Требуется Bearer-токен",
                            headers={"WWW-Authenticate": "Bearer"})
    try:
        payload = jwt.decode(credentials.credentials, JWT_SECRET, algorithms=["HS256"])
        user_id = int(payload["sub"])
    except (jwt.PyJWTError, ValueError, TypeError, KeyError):
        raise HTTPException(status_code=401, detail="Неверный или истекший токен",
                            headers={"WWW-Authenticate": "Bearer"})
    with database() as con:
        if con.execute("SELECT id FROM users WHERE id=?", (user_id,)).fetchone() is None:
            raise HTTPException(status_code=401, detail="Пользователь не найден")
    return user_id


def task_dict(row: sqlite3.Row) -> dict:
    return {key: row[key] for key in ("id", "client_uid", "title", "description", "status", "due_date", "created_at", "updated_at")}


def task_or_error(con: sqlite3.Connection, id_: int, owner: int) -> sqlite3.Row:
    row = con.execute("SELECT * FROM tasks WHERE id=?", (id_,)).fetchone()
    if row is None:
        raise HTTPException(404, "Задача не найдена")
    if row["owner_id"] != owner:
        raise HTTPException(403, "Доступ к чужой задаче запрещён")
    return row


def cache_headers(payload, updated_at: str | None) -> dict:
    body = json.dumps(payload, sort_keys=True, ensure_ascii=False, separators=(",", ":")).encode()
    etag = '"' + sha256(body).hexdigest() + '"'
    dt = datetime.fromisoformat(updated_at) if updated_at else datetime(1970, 1, 1, tzinfo=timezone.utc)
    return {"ETag": etag, "Cache-Control": "private, max-age=0, must-revalidate",
            "Last-Modified": format_datetime(dt, usegmt=True), "Vary": "Authorization, Accept"}


def json_task(response: Response, task: dict):
    for name, value in cache_headers(task, task["updated_at"]).items():
        response.headers[name] = value
    return task


@app.post("/api/v2/auth/login", response_model=Token, tags=["v2 — Авторизация"])
@app.post("/api/v1/auth/login", response_model=Token, tags=["Авторизация"], responses={400: {"description": "Некорректный JSON"}, 401: {"description": "Неверный пароль"}, 500: {"description": "Ошибка сервера"}})
async def login(data: Login):
    with database() as con:
        row = con.execute("SELECT * FROM users WHERE username=?", (data.username,)).fetchone()
    if row is None or not verify_password(data.password, row["password_hash"]):
        raise HTTPException(status_code=401, detail="Неверный логин или пароль")
    token = jwt.encode({"sub": str(row["id"]), "exp": datetime.now(timezone.utc) + timedelta(hours=JWT_LIFETIME_H)},
                       JWT_SECRET, algorithm="HS256")
    return Token(access_token=token, expires_in=JWT_LIFETIME_H * 3600)


@app.get("/api/v1/tasks", response_model=list[TaskOut], tags=["Задачи"], responses={304: {"description": "Данные не изменились"}, 401: {"description": "Нет токена"}, 500: {"description": "Ошибка сервера"}})
async def list_tasks(request: Request, response: Response, owner: int = Depends(current_user)):
    with database() as con:
        rows = con.execute("SELECT * FROM tasks WHERE owner_id=? ORDER BY id", (owner,)).fetchall()
    result = [task_dict(row) for row in rows]
    updated = max((r["updated_at"] for r in rows), default=None)
    headers = cache_headers(result, updated)
    if request.headers.get("If-None-Match") == headers["ETag"]:
        return Response(status_code=304, headers=headers)
    for name, value in headers.items():
        response.headers[name] = value
    return result


@app.get("/api/v2/tasks", response_model=list[TaskOut], tags=["v2 — Задачи"],
         responses={304: {"description": "ETag совпал"}, 400: {"description": "Неверные параметры"},
                    401: {"description": "Нет токена"}})
async def list_tasks_v2(request: Request, response: Response,
                        status: Literal["new", "in_progress", "done"] | None = None,
                        q: str | None = Query(default=None, max_length=120),
                        sort: Literal["id_asc", "id_desc", "due_asc", "due_desc"] = "id_asc",
                        owner: int = Depends(current_user)):
    """v2: filtered/searchable/sortable tasks; response is still a list for v1 clients."""
    with database() as con:
        rows = con.execute("SELECT * FROM tasks WHERE owner_id=? ORDER BY id", (owner,)).fetchall()
    result = [task_dict(row) for row in rows]
    if status is not None:
        result = [task for task in result if task["status"] == status]
    if q:
        needle = q.casefold()
        result = [task for task in result if needle in (task["title"] + " " + task["description"]).casefold()]
    if sort == "id_desc":
        result.sort(key=lambda task: task["id"], reverse=True)
    elif sort.startswith("due_"):
        # Missing due dates are always last; stable ID breaks ties.
        result.sort(key=lambda task: (task["due_date"] is None,
                                      task["due_date"] or "", task["id"]),
                    reverse=False)
        if sort == "due_desc":
            dated = [task for task in result if task["due_date"] is not None]
            undated = [task for task in result if task["due_date"] is None]
            result = list(reversed(dated)) + undated
    updated = max((task["updated_at"] for task in result), default=None)
    headers = cache_headers(result, updated)
    if request.headers.get("If-None-Match") == headers["ETag"]:
        return Response(status_code=304, headers=headers)
    for name, value in headers.items():
        response.headers[name] = value
    return result


@app.get("/api/v2/tasks/summary", response_model=SummaryOut, tags=["v2 — Задачи"])
async def task_summary_v2(request: Request, response: Response, owner: int = Depends(current_user)):
    with database() as con:
        rows = con.execute("SELECT status,due_date,updated_at FROM tasks WHERE owner_id=?", (owner,)).fetchall()
    today = date.today().isoformat()
    result = {"total": len(rows), "new": 0, "in_progress": 0, "done": 0, "overdue": 0}
    for row in rows:
        result[row["status"]] += 1
        if row["status"] != "done" and row["due_date"] is not None and row["due_date"] < today:
            result["overdue"] += 1
    headers = cache_headers(result, max((row["updated_at"] for row in rows), default=None))
    if request.headers.get("If-None-Match") == headers["ETag"]:
        return Response(status_code=304, headers=headers)
    for name, value in headers.items():
        response.headers[name] = value
    return result


@app.get("/api/v2/tasks/{task_id}", response_model=TaskOut, tags=["v2 — Задачи"])
@app.get("/api/v1/tasks/{task_id}", response_model=TaskOut, tags=["Задачи"], responses={304: {"description": "Данные не изменились"}, 403: {"description": "Чужая задача"}, 404: {"description": "Нет задачи"}})
async def get_task(task_id: int, request: Request, response: Response, owner: int = Depends(current_user)):
    with database() as con:
        row = task_or_error(con, task_id, owner)
    task = task_dict(row)
    headers = cache_headers(task, task["updated_at"])
    if request.headers.get("If-None-Match") == headers["ETag"]:
        return Response(status_code=304, headers=headers)
    for name, value in headers.items():
        response.headers[name] = value
    return task


@app.post("/api/v2/tasks", response_model=TaskOut, status_code=201, tags=["v2 — Задачи"])
@app.post("/api/v1/tasks", response_model=TaskOut, status_code=201, tags=["Задачи"], responses={200: {"description": "Повторная отправка client_uid (идемпотентность)"}, 400: {"description": "Неверные поля"}, 401: {"description": "Нет токена"}, 500: {"description": "Ошибка сервера"}})
async def create_task(data: TaskCreate, response: Response, owner: int = Depends(current_user)):
    uid = str(data.client_uid or uuid.uuid4())
    with database() as con:
        old = con.execute("SELECT * FROM tasks WHERE owner_id=? AND client_uid=?", (owner, uid)).fetchone()
        if old:
            response.status_code = 200
            return json_task(response, task_dict(old))
        now = utc_now()
        cur = con.execute("INSERT INTO tasks(owner_id,client_uid,title,description,status,due_date,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?)",
                          (owner, uid, data.title, data.description, data.status, data.due_date.isoformat() if data.due_date else None, now, now))
        row = con.execute("SELECT * FROM tasks WHERE id=?", (cur.lastrowid,)).fetchone()
    return json_task(response, task_dict(row))


@app.put("/api/v2/tasks/{task_id}", response_model=TaskOut, tags=["v2 — Задачи"])
@app.put("/api/v1/tasks/{task_id}", response_model=TaskOut, tags=["Задачи"], responses={400: {"description": "Некорректное тело"}, 403: {"description": "Чужая задача"}, 404: {"description": "Не найдена"}})
async def update_task(task_id: int, data: TaskInput, response: Response, owner: int = Depends(current_user)):
    with database() as con:
        task_or_error(con, task_id, owner)
        now = utc_now()
        con.execute("UPDATE tasks SET title=?,description=?,status=?,due_date=?,updated_at=? WHERE id=?",
                    (data.title, data.description, data.status, data.due_date.isoformat() if data.due_date else None, now, task_id))
        row = con.execute("SELECT * FROM tasks WHERE id=?", (task_id,)).fetchone()
    return json_task(response, task_dict(row))


@app.patch("/api/v2/tasks/{task_id}", response_model=TaskOut, tags=["v2 — Задачи"],
           responses={400: {"description": "Пустой PATCH или некорректные поля"},
                      403: {"description": "Чужая задача"}, 404: {"description": "Нет задачи"}})
async def patch_task_v2(task_id: int, data: TaskPatch, response: Response, owner: int = Depends(current_user)):
    changes = data.model_dump(exclude_unset=True)
    if not changes:
        raise HTTPException(400, "Укажите хотя бы одно поле для изменения")
    if any(value is None for key, value in changes.items() if key != "due_date"):
        raise HTTPException(400, "null разрешён только для due_date")
    with database() as con:
        task_or_error(con, task_id, owner)
        columns = []
        params = []
        for key, value in changes.items():
            columns.append(key + "=?")  # keys are constrained by TaskPatch model
            params.append(value.isoformat() if isinstance(value, date) else value)
        columns.append("updated_at=?")
        params.extend([utc_now(), task_id])
        con.execute("UPDATE tasks SET " + ",".join(columns) + " WHERE id=?", params)
        row = con.execute("SELECT * FROM tasks WHERE id=?", (task_id,)).fetchone()
    return json_task(response, task_dict(row))


@app.delete("/api/v2/tasks/{task_id}", status_code=204, tags=["v2 — Задачи"])
@app.delete("/api/v1/tasks/{task_id}", status_code=204, tags=["Задачи"], responses={401: {"description": "Нет токена"}, 403: {"description": "Чужая задача"}, 404: {"description": "Нет задачи"}})
async def delete_task(task_id: int, owner: int = Depends(current_user)):
    with database() as con:
        task_or_error(con, task_id, owner)
        con.execute("DELETE FROM tasks WHERE id=?", (task_id,))
    return Response(status_code=204)


@app.get("/api/v2/debug/error", tags=["v2 — Тестирование ошибок"])
@app.get("/api/v1/debug/error", tags=["Демонстрация ошибок"], include_in_schema=True, responses={403: {"description": "Демо-режим выключен"}, 500: {"description": "Учебная серверная ошибка"}})
async def show_server_error(owner: int = Depends(current_user)):
    if os.getenv("TASKS_DEMO_ERRORS", "0") != "1":
        raise HTTPException(403, "Демонстрация 500 отключена")
    return JSONResponse(status_code=500, content={"detail": "Учебная ошибка 500 (TASKS_DEMO_ERRORS=1)"})


@app.get("/", include_in_schema=False)
async def index():
    return FileResponse(ROOT / "public" / "index.html")


app.mount("/static", StaticFiles(directory=ROOT / "public"), name="static")
