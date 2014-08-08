//===========================================
//  Lumina-DE source code
//  Copyright (c) 2012, Ken Moore
//  Available under the 3-clause BSD license
//  See the LICENSE file for full details
//===========================================
#ifndef _LUMINA_DESKTOP_SESSION_H
#define _LUMINA_DESKTOP_SESSION_H

#include <QApplication>
#include <QDebug>
#include <QString>
#include <QX11Info>
#include <QEvent>
#include <QTranslator>
#include <QSettings>
#include <QProxyStyle>
#include <QDesktopWidget>
#include <QList>

#include "Globals.h"
#include "AppMenu.h"
#include "SettingsMenu.h"
#include "SystemWindow.h"
#include "LDesktop.h"
#include "WMProcess.h"

//LibLumina X11 class
#include <LuminaX11.h>

#include <Phonon/MediaObject>
#include <Phonon/AudioOutput>

//SYSTEM TRAY STANDARD DEFINITIONS
#define SYSTEM_TRAY_REQUEST_DOCK 0
#define SYSTEM_TRAY_BEGIN_MESSAGE 1
#define SYSTEM_TRAY_CANCEL_MESSAGE 2

class MenuProxyStyle : public QProxyStyle{
public: 
	int pixelMetric(PixelMetric metric, const QStyleOption *option=0, const QWidget *widget=0) const{
	  if(metric==PM_SmallIconSize){ return 22; } //override QMenu icon size (make it larger)
	  else{ return QProxyStyle::pixelMetric(metric, option, widget); } //use the current style for everything else
	}
};

class LSession : public QApplication{
	Q_OBJECT
public:
	LSession(int &argc, char **argv);
	~LSession();
	//Functions to be called during startup
	void setupSession();

	virtual bool x11EventFilter(XEvent *event);
	
	bool LoadLocale(QString);

	//System Tray Utilities
	static bool StartupSystemTray();
	static bool CloseSystemTray();
	//System Access
	static AppMenu* applicationMenu();
	static void systemWindow();
	static SettingsMenu* settingsMenu();

private:
	WMProcess *WM;
	QList<LDesktop*> DESKTOPS;
	QFileSystemWatcher *watcher;
	//Audio Output variables
	Phonon::MediaObject *mediaObj;
	Phonon::AudioOutput *audioOut;
	QFile *audioFile;

public slots:
	void launchStartupApps();
	void playStartupAudio();
	void playLogoutAudio();


private slots:
	void watcherChange(QString);

	//Internal simplification functions
	void checkUserFiles();
	void loadStyleSheet();
	void refreshWindowManager();
	void updateDesktops();

	void audioOutputFinished();

	//system tray functions
	//void parseClientMessageEvent(XClientMessageEvent *event);

signals:
	void NewSystemTrayApp(WId); //WinID
	void WindowListEvent(WId);
	void WindowListEvent();
	void LocaleChanged();
	void DesktopConfigChanged();
	
};

#endif
