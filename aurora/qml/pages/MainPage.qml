import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page
    allowedOrientations: Orientation.All
    property int selectedLocalId: -1

    function selectedStatus() {
        if (statusCombo.currentIndex === 1) return "in_progress"
        if (statusCombo.currentIndex === 2) return "done"
        return "new"
    }
    function resetEditor() {
        selectedLocalId = -1
        titleField.text = ""
        descriptionField.text = ""
        dueField.text = ""
        statusCombo.currentIndex = 0
    }
    function loadTask(t) {
        selectedLocalId = t.localId
        titleField.text = t.title
        descriptionField.text = t.description
        dueField.text = t.dueDate
        statusCombo.currentIndex = t.status === "done" ? 2 : (t.status === "in_progress" ? 1 : 0)
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: content.height + Theme.paddingLarge
        VerticalScrollDecorator { }
        PullDownMenu {
            MenuItem { text: "Синхронизировать"; onClicked: backend.sync() }
            MenuItem { text: "Очистить форму"; onClicked: page.resetEditor() }
        }

        Column {
            id: content
            width: parent.width
            spacing: Theme.paddingSmall

            PageHeader { title: "Список задач" }

            SectionHeader { text: "Сервер HTTPS" }
            TextField {
                id: addressField
                width: parent.width
                label: "URL сервера"
                placeholderText: "https://192.168.1.100:8443"
                text: backend.serverUrl
                inputMethodHints: Qt.ImhUrlCharactersOnly
            }
            TextArea {
                id: fingerprintField
                width: parent.width
                label: "SHA-256 отпечаток сертификата"
                placeholderText: "64 шестнадцатеричных символа"
                text: backend.fingerprint
                wrapMode: TextEdit.WrapAnywhere
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Сохранить HTTPS-настройки"
                enabled: !backend.busy
                onClicked: backend.configure(addressField.text, fingerprintField.text)
            }
            TextSwitch {
                text: "Offline-режим"
                description: "Не отправлять запросы; сохранять изменения в SQLite"
                checked: backend.offline
                onClicked: backend.setOffline(checked)
            }
            Label {
                width: parent.width - Theme.horizontalPageMargin * 2
                x: Theme.horizontalPageMargin
                text: backend.message
                color: backend.offline ? Theme.highlightColor : Theme.primaryColor
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
            }
            Label {
                width: parent.width - Theme.horizontalPageMargin * 2
                x: Theme.horizontalPageMargin
                text: backend.busy ? "Выполняется запрос…" : (backend.authenticated ? "Пользователь: " + backend.username : "Нет активного токена")
                color: Theme.secondaryColor
                wrapMode: Text.Wrap
            }

            SectionHeader { text: "Вход (JWT)" }
            TextField {
                id: usernameField
                width: parent.width
                label: "Логин"
                text: backend.username.length ? backend.username : "student"
                placeholderText: "student"
            }
            TextField {
                id: passwordField
                width: parent.width
                label: "Пароль"
                placeholderText: "student123"
                echoMode: TextInput.Password
            }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.paddingMedium
                Button {
                    text: "Войти"
                    enabled: !backend.offline && !backend.busy
                    onClicked: { backend.login(usernameField.text, passwordField.text); passwordField.text = "" }
                }
                Button {
                    text: "Выйти"
                    enabled: !backend.busy
                    onClicked: backend.logout()
                }
            }

            SectionHeader { text: selectedLocalId < 0 ? "Новая задача" : "Редактирование задачи" }
            TextField {
                id: titleField
                width: parent.width
                label: "Заголовок"
                placeholderText: "Название задачи"
            }
            TextArea {
                id: descriptionField
                width: parent.width
                label: "Описание"
                placeholderText: "Описание задачи"
            }
            ComboBox {
                id: statusCombo
                width: parent.width
                label: "Статус"
                menu: ContextMenu {
                    MenuItem { text: "Новая" }
                    MenuItem { text: "В работе" }
                    MenuItem { text: "Готово" }
                }
            }
            TextField {
                id: dueField
                width: parent.width
                label: "Срок (YYYY-MM-DD)"
                placeholderText: "2026-12-31 или пусто"
            }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.paddingMedium
                Button {
                    text: selectedLocalId < 0 ? "Создать" : "Сохранить"
                    enabled: !backend.busy && backend.username.length > 0
                    onClicked: {
                        if (titleField.text.trim().length === 0) return
                        if (selectedLocalId < 0)
                            backend.createTask(titleField.text, descriptionField.text, selectedStatus(), dueField.text)
                        else
                            backend.updateTask(selectedLocalId, titleField.text, descriptionField.text, selectedStatus(), dueField.text)
                        page.resetEditor()
                    }
                }
                Button { text: "Отменить"; enabled: !backend.busy; onClicked: page.resetEditor() }
            }

            SectionHeader { text: "Локальные задачи (SQLite)" }
            Label {
                visible: backend.tasks.length === 0
                width: parent.width - Theme.horizontalPageMargin * 2
                x: Theme.horizontalPageMargin
                text: "Задач пока нет. Войдите и синхронизируйте либо создайте задачу."
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
            }
            Repeater {
                model: backend.tasks
                delegate: Column {
                    width: content.width
                    spacing: Theme.paddingSmall
                    Item { width: 1; height: Theme.paddingSmall }
                    Label {
                        x: Theme.horizontalPageMargin
                        width: parent.width - Theme.horizontalPageMargin * 2
                        text: modelData.title
                        wrapMode: Text.Wrap
                        font.pixelSize: Theme.fontSizeLarge
                        color: Theme.highlightColor
                    }
                    Label {
                        x: Theme.horizontalPageMargin
                        width: parent.width - Theme.horizontalPageMargin * 2
                        text: modelData.description.length ? modelData.description : "Без описания"
                        wrapMode: Text.Wrap
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.secondaryColor
                    }
                    Label {
                        x: Theme.horizontalPageMargin
                        width: parent.width - Theme.horizontalPageMargin * 2
                        text: "Статус: " + (modelData.status === "done" ? "Готово" : modelData.status === "in_progress" ? "В работе" : "Новая") +
                              " · Срок: " + (modelData.dueDate || "нет") +
                              (modelData.dirty !== "none" ? " · ОЖИДАЕТ СИНХРОНИЗАЦИИ" : " · синхронизировано")
                        wrapMode: Text.Wrap
                        font.pixelSize: Theme.fontSizeExtraSmall
                    }
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: Theme.paddingMedium
                        Button { text: "Изменить"; enabled: !backend.busy; onClicked: page.loadTask(modelData) }
                        Button { text: "Удалить"; enabled: !backend.busy; onClicked: backend.deleteTask(modelData.localId) }
                    }
                    Rectangle { width: parent.width; height: 1; color: Theme.secondaryColor; opacity: 0.2 }
                }
            }
        }
    }
}
