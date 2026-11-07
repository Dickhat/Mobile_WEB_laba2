"use strict";
const $ = (selector) => document.querySelector(selector);

let token = sessionStorage.getItem("tasksToken") || "";
let editingId = null;
let tasks = [];
let lastETag = "";

function show(message) {
  $("#message").textContent = message;
}

function authState() {
  $("#authStatus").textContent = token
    ? "Авторизация выполнена. Доступ к API открыт."
    : "Войдите, чтобы работать с задачами.";
}
  async function api(path, method="GET", payload, extraHeaders={}) {
    const headers = {Accept: "application/json", ...extraHeaders};

    if (payload !== undefined) headers["Content-Type"] = "application/json";
    if (token)                 headers.Authorization   = `Bearer ${token}`;

    // Host/User-Agent устанавливаются самим браузером: JS не вправе переопределять их.
    const response = await fetch(`/api/v1${path}`, { method, headers, body: payload === undefined ? undefined : JSON.stringify(payload), cache: "no-store"});

    if (response.status === 304) return {status: 304, etag: response.headers.get("ETag"), data: null};

    const text = await response.text();

    let data;
    try   { data = text ? JSON.parse(text) : null; }
    catch { data = {detail: text};}

    if (response.status === 401 && path !== "/auth/login") {
      token    = "";
      tasks    = [];
      lastETag = "";
      sessionStorage.removeItem("tasksToken");

      authState();
      $("#tasks").textContent = "Сессия истекла. Войдите заново.";

      throw new Error("Сессия истекла. Требуется повторный вход.");
    }

    return {status: response.status, data, etag: response.headers.get("ETag")};
}

function taskCard(task) {
  const card = document.createElement("article");
  card.className = "task";

  const h = document.createElement("h3");
  h.textContent = task.title;
  card.append(h);

  const p = document.createElement("p");
  p.textContent = task.description || "Без описания";
  card.append(p);

  const meta = document.createElement("div");
  meta.className = "meta";

  const names = {
    new: "Новая",
    in_progress: "В работе",
    done: "Готово",
  };

  meta.textContent = `№ ${task.id} · ${names[task.status]} · Срок: ${task.due_date || "не указан"}`;
  card.append(meta);

  const actions = document.createElement("div");
  actions.className = "actions";

  const edit = document.createElement("button");
  edit.textContent = "Редактировать";
  edit.className = "secondary";
  edit.addEventListener("click", () => {
    editingId = task.id;
    $("#title").value = task.title;
    $("#description").value = task.description || "";
    $("#status").value = task.status;
    $("#dueDate").value = task.due_date || "";
    $("#editorTitle").textContent = `Изменить задачу №${task.id}`;
    $("#save").textContent = "Сохранить";
    $("#cancelEdit").hidden = false;
    $("#title").focus();
  });

  const del = document.createElement("button");
  del.textContent = "Удалить";
  del.className = "danger";
  del.addEventListener("click", async () => {
    if (!confirm(`Удалить «${task.title}»?`)) {
      return;
    }

    try {
      await api(`/tasks/${task.id}`, "DELETE");

      if (editingId === task.id) { resetEditor(); }

      lastETag = "";
      await reload();
      show("Задача удалена (204).");
    } 
    catch (error) {
      show(error.message);
    }
  });

  actions.append(edit, del);
  card.append(actions);

  return card;
}

function resetEditor() {
  editingId = null;
  $("#taskForm").reset();
  $("#editorTitle").textContent = "Новая задача";
  $("#save").textContent = "Создать задачу";
  $("#cancelEdit").hidden = true;
}

async function reload() {
  if (!token) {
    $("#tasks").textContent = "Сначала войдите.";
    return;
  }

  try {
    // no-store forces request; conditional ETag still explicitly sent to API.
    const response = await api("/tasks", "GET", undefined, lastETag ? { "If-None-Match": lastETag } : {});

    if (response.status === 304) {
      show("HTTP 304 — ETag совпал, задачи не изменились.");
      return;
    }

    lastETag = response.etag || "";
    tasks = response.data;

    $("#cache").textContent = "ETag: " + (lastETag || "отсутствует");
    $("#tasks").replaceChildren();

    if (!tasks.length) {
      $("#tasks").textContent = "Пока нет задач.";
    } else {
      tasks.forEach((task) => $("#tasks").append(taskCard(task)));
    }

    show(`Загружено задач: ${tasks.length} (HTTP 200).`);
  } catch (error) {
    show(error.message);
  }
}

$("#loginForm").addEventListener("submit", async (event) => {
  event.preventDefault();

  try {
    const { data } = await api("/auth/login", "POST", {
      username: $("#login").value.trim(),
      password: $("#password").value,
    });

    token = data.access_token;
    sessionStorage.setItem("tasksToken", token);
    lastETag = "";

    authState();
    show("Вход выполнен.");

    await reload();
  } catch (error) {
    show(error.message);
  }
});

$("#logout").addEventListener("click", () => {
  token = "";
  tasks = [];
  lastETag = "";
  sessionStorage.removeItem("tasksToken");

  $("#tasks").textContent = "Сначала войдите.";
  resetEditor();
  authState();
  show("Выход выполнен.");
});

$("#taskForm").addEventListener("submit", async (event) => {
  event.preventDefault();

  if (!token) {
    show("Сначала войдите.");
    return;
  }

  const payload = {
    title: $("#title").value.trim(),
    description: $("#description").value,
    status: $("#status").value,
    due_date: $("#dueDate").value || null,
  };

  try {
    const method = editingId === null ? "POST" : "PUT";
    const path = editingId === null ? "/tasks" : `/tasks/${editingId}`;
    const { status } = await api(path, method, payload);

    resetEditor();
    lastETag = "";
    await reload();
    show(`Операция выполнена: HTTP ${status}.`);
  } 
  catch (error) {
    show(error.message);
  }
});

$("#cancelEdit").addEventListener("click", resetEditor);
$("#reload").addEventListener("click", reload);

authState();
if (token) {
  reload();
}
