/****************************************************************************
**
** Copyright (C) 2017-2020 Elros https://github.com/elros34
**               2020 Rinigus https://github.com/rinigus
**               2012 Digia Plc and/or its subsidiary(-ies).
**
** This file is part of Flatpak Runner.
**
** You may use this file under the terms of the BSD license as follows:
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * Neither the name of the copyright holder nor the names of its
**     contributors may be used to endorse or promote products derived
**     from this software without specific prior written permission.
**
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
**
**
****************************************************************************/

#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickView>
#include <QStandardPaths>
#include <QDir>
#include <sailfishapp.h>

#include <iostream>
#include <string.h>

#include "appsettings.h"
#include "dbuscontainerstate.h"
#include "imageconverter.h"
#include "qmlcompositor.h"
#include "runner.h"

QString getFreeWaylandSocket()
{
  QDir runtime(QStandardPaths::standardLocations(QStandardPaths::RuntimeLocation).first());
  QString relative_base = "../../display";
  // limit search for the limited number of displays
  for (int i=0; i < 1000; ++i)
    {
      QString wd = QString("%1/wayland-%2").arg(relative_base).arg(i);
      if (!runtime.exists(wd))
        return wd;
    }
  return QString();
}

int main(int argc, char *argv[])
{
    QStringList posopt;
    for (int i=1; i<argc; ++i)
      posopt.append(argv[i]);

    if (posopt.length() >= 1 && posopt[0] == "--help")
      {
        std::cout << "\nFlatpak Runner: helper for running Flatpak applications\n"
                  << "\nUsage:\n"
                  << argv[0] << " [flatpak run options] applicationID [application options]\n"
                  << "\nWhen started without arguments, it will open Wayland server with the corresponding socket printed out in standard output\n"
                  << "\nOther recognized options:\n"
                  << " --help This help screen\n"
                  << " other Qt options, as passed to Qt application\n"
                  << "\n";
        return 0;
      }

    bool run = !posopt.isEmpty();

    QStringList flatpak_options;
    QString program;
    QStringList program_options;

    if (run)
      {
        for (QString &s: posopt) {
            bool isopt = s.startsWith('-');
            if (isopt && program.isEmpty()) flatpak_options.append(s);
            else if (isopt || !program.isEmpty()) program_options.append(s);
            if (!isopt && program.isEmpty())
              program = s;
          }

        std::cout << "Starting: " << program.toStdString() << "\n";

        if (!flatpak_options.isEmpty())
          std::cout << "Flatpak run options: " << flatpak_options.join(' ').toStdString() << "\n";

        if (!program_options.isEmpty())
          std::cout << "Program options: " << program_options.join(' ').toStdString() << "\n";
      }
    else
      std::cout << "Starting empty Wayland server and enabling settings\n";

    // create filtered arguments
    int sfos_argc = 1;
    char **sfos_argv = nullptr;
    if (program.isEmpty()) {
        sfos_argc = argc;
        sfos_argv = argv;
    } else {
        sfos_argc = 1;
        sfos_argv = (char**)malloc(sizeof(char*)*1);
        std::string s = program.toStdString();
        sfos_argv[0] = (char*)malloc(strlen(s.c_str()) + 1);
        strcpy(sfos_argv[0], s.c_str());
    }
    std::cout << "Starting as: " << sfos_argv[0] << "\n";
    QScopedPointer<QGuiApplication> app(SailfishApp::application(sfos_argc, sfos_argv));
    app->setApplicationName("flatpak-runner");
    app->setOrganizationName("flatpak-runner");
    app->setApplicationVersion(APP_VERSION);

    QScopedPointer<QQuickView> view(SailfishApp::createView());

    // compositor
    QString socket = getFreeWaylandSocket();
    if (socket.isEmpty())
      {
        std::cerr << "Cannot find empty socket\n";
        return -1;
      }
    else
      std::cerr << "Wayland socket: " << socket.toStdString() << "\n";
    QmlCompositor compositor(view.data(), socket.toStdString().c_str());
    QObject::connect(view.data(), SIGNAL(afterRendering()), &compositor, SLOT(sendCallbacks()));
    view->rootContext()->setContextProperty("compositor", &compositor);

    // application settings
    AppSettings settings;
    view->rootContext()->setContextProperty("settings", &settings);

    // dbus service
    DBusContainerState dbuscontainer(compositor.window());

    // runner
    Runner runner(program, flatpak_options, program_options, socket,
                  dbuscontainer.address(), settings);
    view->rootContext()->setContextProperty("runner", &runner);

    // image converter
    ImageConverter imageconv;
    view->rootContext()->setContextProperty("imageConverter", &imageconv);

    // whether settings are allowed and misc variables
    view->rootContext()->setContextProperty("modeSettings", !run);
    view->rootContext()->setContextProperty("programVersion", APP_VERSION);

    //view->setSource(SailfishApp::pathTo("qml/main.qml"));
    view->setSource(QUrl("file:///usr/share/flatpak-runner/qml/main.qml"));
    view->create();
    QObject *firstPage = view->rootObject()->findChild<QObject*>("mainPage");
    QObject::connect(&compositor, SIGNAL(windowAdded(QVariant)), firstPage, SLOT(windowAdded(QVariant)));
    QObject::connect(&compositor, SIGNAL(windowResized(QVariant)), firstPage, SLOT(windowResized(QVariant)));

    view->show();
    app->processEvents();
    runner.start();

    return app->exec();
}

