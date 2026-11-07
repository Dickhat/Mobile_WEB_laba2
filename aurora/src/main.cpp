#include <auroraapp.h>
#include <QtQuick>
#include <QQmlContext>
#include "TaskBackend.h"

int main(int argc, char *argv[])
{
    QScopedPointer<QGuiApplication> app(Aurora::Application::application(argc, argv));
    app->setOrganizationName(QStringLiteral("ru.mai"));
    app->setApplicationName(QStringLiteral("lab2tasks"));
    TaskBackend backend;
    QScopedPointer<QQuickView> view(Aurora::Application::createView());
    view->rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    view->setSource(Aurora::Application::pathTo(QStringLiteral("qml/lab2tasks.qml")));
    view->show();
    return app->exec();
}
