import QtQuick 2.0
import Sailfish.Silica 1.0
CoverBackground {
    Column {
        anchors.centerIn: parent
        width: parent.width - Theme.paddingLarge * 2
        spacing: Theme.paddingMedium
        Label { width: parent.width; text: "Список задач"; horizontalAlignment: Text.AlignHCenter; color: Theme.highlightColor }
        Label { width: parent.width; text: backend.offline ? "OFFLINE" : "REST / HTTPS"; horizontalAlignment: Text.AlignHCenter }
        Label { width: parent.width; text: backend.tasks.length + " задач"; horizontalAlignment: Text.AlignHCenter }
    }
}
