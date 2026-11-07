# REST API v1 — примеры запросов

Базовый URL: `https://192.168.1.100:8443` (**замени IP на IP ПК**).

## Заголовки

- `Content-Type: application/json` для POST/PUT.
- `Accept: application/json` для JSON-ответов.
- `Authorization: Bearer <JWT>` для всех запросов `/tasks`.
- `User-Agent: AuroraTasksLab2/1.0 QtNetwork` задаёт C++ клиент; браузер задаёт автоматически.
- `Host: 192.168.1.100:8443` автоматически задаётся клиентом из HTTPS URL (в браузере изменение запрещено).
- Ответ GET: `ETag`, `Cache-Control: private, max-age=0, must-revalidate`, `Last-Modified`, `Vary`.

## Эндпоинты

| Метод | Путь | Назначение | Успех |
|---|---|---|---|
| POST | `/api/v1/auth/login` | Логин, получение JWT | 200 |
| GET | `/api/v1/tasks` | Мои задачи | 200, 304 |
| GET | `/api/v1/tasks/{id}` | Одна задача | 200, 304 |
| POST | `/api/v1/tasks` | Создать задачу | 201, 200 (повтор client_uid) |
| PUT | `/api/v1/tasks/{id}` | Полностью изменить | 200 |
| DELETE | `/api/v1/tasks/{id}` | Удалить | 204 |
| GET | `/api/v1/debug/error` | Учебная ошибка 500 | 403 или 500 |

## Формат задачи

```json
{
  "id": 42,
  "client_uid": "4d60a10b-595a-4994-87ea-8759bd4c4f69",
  "title": "Подготовить отчет",
  "description": "Скриншоты Авроры и Swagger",
  "status": "in_progress",
  "due_date": "2026-12-31",
  "created_at": "2026-11-07T10:00:00.000000+00:00",
  "updated_at": "2026-11-07T10:00:00.000000+00:00"
}
```

При POST поля `id`, `created_at`, `updated_at` передавать **не нужно**. `client_uid` необязателен для браузера, **обязателен для надёжной offline-синхронизации** (создаётся на клиенте один раз и сохраняется при повторах). Статусы: `new`, `in_progress`, `done`. `due_date`: дата `YYYY-MM-DD` или `null`.

## Проверка curl

Для быстрой диагностики самоподписанного HTTPS на доверенной учебной машине в curl можно использовать `-k` (только для проверки; обычный браузер и мобильное приложение так не делают):

```bash
curl -k -i https://192.168.1.100:8443/api/v1/tasks
# HTTP 401

curl -k -X POST https://192.168.1.100:8443/api/v1/auth/login \
  -H 'Accept: application/json' -H 'Content-Type: application/json' \
  -d '{"username":"student","password":"student123"}'
# скопируй access_token в переменную TOKEN

curl -k -i https://192.168.1.100:8443/api/v1/tasks \
  -H "Authorization: Bearer $TOKEN" -H 'Accept: application/json'

curl -k -i -X POST https://192.168.1.100:8443/api/v1/tasks \
  -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' \
  -d '{"title":"Тестовая задача","description":"ЛР №2","status":"new","due_date":null}'
# HTTP 201, в ответе поле id

curl -k -i -X PUT https://192.168.1.100:8443/api/v1/tasks/1 \
  -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' \
  -d '{"title":"Исправленная задача","description":"Готово","status":"done","due_date":null}'
# HTTP 200

curl -k -i -X DELETE https://192.168.1.100:8443/api/v1/tasks/1 \
  -H "Authorization: Bearer $TOKEN"
# HTTP 204
```

Для ETag: сохрани заголовок `ETag` из GET в `ETAG`, затем передай `-H "If-None-Match: $ETAG"`. Сервер ответит `304 Not Modified`.

Для `400`: POST с пустым заголовком; для `401`: отсутствующий/неправильный токен; для `403`: прочитать задачу пользователя `student` с JWT `guest`; для `404`: GET несуществующего ID; для `500`: задать `TASKS_DEMO_ERRORS=1` в окружении и вызвать авторизованный `GET /api/v1/debug/error`.

## OpenAPI

FastAPI автоматически создаёт OpenAPI 3.1 JSON по адресу `/openapi.json`, Swagger UI `/docs`. Статическая копия сгенерированной схемы — `docs/openapi.json`.
