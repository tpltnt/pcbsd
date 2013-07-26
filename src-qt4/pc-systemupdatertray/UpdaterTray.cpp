/* Qt */
#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QImage>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <QTimer>
#include <QToolTip>
#include <QTranslator>
#include <QTextStream>

#include "UpdaterTray.h"
#include "../config.h"

/* Update interval in ms  - Every 24 hours */
#define  UPDATE_MSEC 1000 * 60 * 60 * 24 

/* Change this to switch the default patch tmpdir */
const QString PATCHTMPDIR_DEFAULT( PREFIX + "/tmp" );

/* The Update STATUS Flags */
#define SYSTEM_UP2DATE 0
#define SYSTEM_UPDATE_AVAIL 1
#define PACKAGE_UPDATE_AVAIL 2
#define SYSTEM_CHECKING4UPDATES 4
#define SYSTEM_UPDATING 5
#define CHECK_FAILED 6
#define SYSTEM_RESTART_NEEDED 7

#define WARDEN_UP2DATE 0
#define WARDEN_UPDATE_AVAIL 1

#define PBI_UPDATED 0
#define PBI_UPDATES_AVAIL 1
#define PBI_CHECKING4UPDATES 2
#define PBI_UPDATING 3

// Public Variables
QString username;
int programstatus = SYSTEM_UP2DATE;
int wardenstatus = WARDEN_UP2DATE;
int pbistatus = PBI_UPDATED;
bool checkSysUpdatesFrequently = TRUE;
bool checkPBIUpdatesFrequently = TRUE;
int currentSysWorkingItem;

// Define our processes
QProcess *getUpdatesDir;
QProcess *readSysUpdates;
QProcess *listingProc;
QProcess *checkPBIProc;
QProcess *launchAdminProc;
  
QTimer *sysTimer;
QTimer *pbiTimer;

QString PBIProgName[900];
QString PBIProgVer[900];
QString PBIProgNewVer[900];
QString PBIProgIcon[900];
QString PBIProgURL1[900];
QString PBIProgURL2[900];
QString PBIProgURL3[900];
QString PBIProgMD5[900];
QString PBIProgMdate[900];
QString PBIProgLoc[900];
int     PBIProgUpdate[900];
int     PBIProgFailed[900];
QString PBIBuffer;
QString updatePBITextList;

// Updater frequency supplied by user
qlonglong	updateFrequency;

#define AUTOINACTIVE 0
#define AUTODOWNLOAD 1
#define AUTOFINISHED 2
#define AUTOFINISHEDDL 3
#define AUTOINSTALLING 4
#define AUTOFAILED 5

void UpdaterTray::programInit()
{
  QString tmp, command;
  autoStatus = AUTOINACTIVE;
  doingCheck=false;
  shownPopup=false;

  // Use built-in frequency until we load another
  updateFrequency = -1;

  sysTimer = new QTimer(this);
  pbiTimer = new QTimer(this);

  connect( sysTimer, SIGNAL(timeout()), this, SLOT(slotScheduledSystemCheck()) );
  connect( pbiTimer, SIGNAL(timeout()), this, SLOT(slotScheduledPBICheck()) );

  // Get the username of the person running X
  username = getlogin();

  // Setup our Context Menu
  QIcon contextIcon;
  contextIcon.addFile(":/images/updated.png");


  trayIconMenu = new QMenu(this);
  trayIconMenu->setIcon(contextIcon);
  trayIconMenu->addSeparator();
  trayIconMenu->addAction( QIcon(":/images/sysupdater.png"), tr("Start the Update Manager"),  this, SLOT(slotOpenUpdateManager()));
  trayIconMenu->addAction( QIcon(":/images/pkgmanager.png"), tr("Start the Package Manager"), this, SLOT(slotOpenPackageManager()));
  trayIconMenu->addSeparator();
  trayIconMenu->addAction( QIcon(":/images/appcafe.png"),    tr("Start the AppCafe"), this, SLOT(slotOpenSoftwareManager()));
  trayIconMenu->addAction( QIcon(":/images/warden.png"),     tr("Start the Warden"),  this, SLOT(slotOpenJailManager()));
  trayIconMenu->addSeparator();
  trayIconMenu->addAction( QIcon(":/images/view-refresh.png"),tr("Check for updates"),this, SLOT(slotCheckAllUpdates()));
  trayIconMenu->addSeparator();
  runAction = trayIconMenu->addAction( tr("Run at startup"), this, SLOT(slotChangeRunStartup()) );
  runAction->setCheckable( TRUE );
  runAction->setChecked( TRUE );
  popAction = trayIconMenu->addAction( tr("Display notifications"), this, SLOT(slotChangePopup()) );
  popAction->setCheckable( TRUE );
  popAction->setChecked( TRUE );
  trayIconMenu->addSeparator();
  trayIconMenu->addAction( tr("Quit"), this, SLOT(slotQuitTray()) );
  
  // Init the tray icon
  trayIcon = new QSystemTrayIcon(this);
  trayIcon->setContextMenu(trayIconMenu);
  QIcon Icon(":/images/working.png");
  trayIcon->setIcon(Icon);
  trayIcon->show();
  connect( trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(slotTrayActivated(QSystemTrayIcon::ActivationReason) ) );

  // Load the program preferences
  loadUpdaterPrefs();

  // Start the monitor service for system updates
  QTimer::singleShot(1000, this, SLOT(slotScheduledSystemCheck()));

  // Start the monitor service for PBI updates
  QTimer::singleShot(60000, this, SLOT(slotScheduledPBICheck()));

  // Monitor if we need to start any update checks
  QTimer::singleShot(500, this, SLOT(slotMonitorForChanges()));

  // Watch our trigger file, to see if any automated updates are being downloaded
  QFile sysTrig( SYSTRIGGER );
  if ( sysTrig.open( QIODevice::WriteOnly ) ) {
    QTextStream streamTrig( &sysTrig );
     streamTrig << QDateTime::currentDateTime().toString("hhmmss");
  }
  sysTrig.close();

  // Start our file watchers
  fileWatcherAutoUpdate = new QFileSystemWatcher();
  fileWatcherAutoUpdate->addPath(SYSTRIGGER);
  connect(fileWatcherAutoUpdate, SIGNAL(fileChanged(const QString&)), this, SLOT(slotSetTimerReadAutoStatus() ));

  // Watch for PBI updates and refresh
  QFile pbiTrig( PBITRIGGER );
  if ( pbiTrig.open( QIODevice::WriteOnly ) ) {
    QTextStream streamTrig1( &pbiTrig );
     streamTrig1 << QDateTime::currentDateTime().toString("hhmmss");
     pbiTrig.close();
  }
  pbiWatcherAutoUpdate = new QFileSystemWatcher();
  pbiWatcherAutoUpdate->addPath(PBITRIGGER);
  connect(pbiWatcherAutoUpdate, SIGNAL(fileChanged(const QString&)), this, SLOT(slotScheduledPBICheck() ));

  // Watch our trigger file, to see if any automated updates are being downloaded
  fileWatcherSys = new QFileSystemWatcher();
  fileWatcherSys->addPath(PREFIX + "/share/pcbsd/pc-updatemanager/conf/sysupdate.conf");
  connect(fileWatcherSys, SIGNAL(fileChanged(const QString&)), this, SLOT(slotScheduledSystemCheck() ));
}

// Slot that watches for any config changes
void UpdaterTray::slotMonitorForChanges()
{
  QString upstamp, userupstamp;
  qlonglong oldfrequency;
  bool needRescan = false;

  QSettings settings("PCBSD");

  // Load the update frequency
  oldfrequency = updateFrequency;
  updateFrequency = settings.value("/PC-BSD/SystemUpdater/checkUpdateFrequency", updateFrequency).toLongLong();
  if ( oldfrequency != updateFrequency)
    needRescan = true;


  // Check if we need to re-scan for PBI / system updates
  upstamp = settings.value( "/PC-BSD/SystemUpdater/updateStatusTimeStamp", QString() ).toString();
  userupstamp = settings.value( "/PC-BSD/SystemUpdater/updateStatusUserTimeStamp", QString() ).toString();
  if ( upstamp != userupstamp ) {
    settings.setValue( "/PC-BSD/SystemUpdater/updateStatusUserTimeStamp", upstamp );
    needRescan = true;
  }

  if ( needRescan ) {
    slotScheduledSystemCheck();
    slotScheduledPBICheck();
  }

  QTimer::singleShot(5000, this, SLOT(slotMonitorForChanges()));
}

void UpdaterTray::slotSetTimerReadAutoStatus()
{
  QTimer::singleShot(1000, this, SLOT(slotAutoUpdateStatusChanged()));
}

void UpdaterTray::slotAutoUpdateStatusChanged()
{
  QString line;
  QFile file( SYSTRIGGER );
  if ( file.open( QIODevice::ReadOnly ) ) {
      QTextStream stream( &file );
      while ( !stream.atEnd() ) {
          line = stream.readLine(); // line of text excluding '\n'
          if ( line.indexOf("DOWNLOADING: " ) == 0)
          {
              line.replace("DOWNLOADING: ", "");
              autoStatus = AUTODOWNLOAD;
              autoCurrentUpdate = line;
          }
          if ( line.indexOf("INSTALLFAILED: " ) == 0)
          {
              autoStatus = AUTOFINISHED;
              QTimer::singleShot(2000, this, SLOT(slotStartUpdateCheck()));
	      qDebug() << "FAILED INSTALLING UPDATES";
          }
          if ( line.indexOf("INSTALLFINISHED: " ) == 0)
          {
              autoStatus = AUTOFINISHED;
              QTimer::singleShot(2000, this, SLOT(slotStartUpdateCheck()));
	      qDebug() << "DONE INSTALLING UPDATES";
          }
      } 

      contextMenuRefresh();
   }
   file.close();

   if ( ! file.exists() ) {
     QFile sysTrig( SYSTRIGGER );
     if ( sysTrig.open( QIODevice::WriteOnly ) ) {
       QTextStream streamTrig( &sysTrig );
       streamTrig << QDateTime::currentDateTime().toString("hhmmss");
     }
     sysTrig.close();

     // Start our file watchers
     fileWatcherAutoUpdate = new QFileSystemWatcher();
     fileWatcherAutoUpdate->addPath(SYSTRIGGER);
     connect(fileWatcherAutoUpdate, SIGNAL(fileChanged(const QString&)), this, SLOT(slotSetTimerReadAutoStatus() ));
   }

}

void UpdaterTray::slotCheckAllUpdates()
{
   // Start the monitor service for system updates
  QTimer::singleShot(300, this, SLOT(slotScheduledSystemCheck()));

  // Start the monitor service for PBI updates
  QTimer::singleShot(400, this, SLOT(slotScheduledPBICheck()));

}

void UpdaterTray::slotChangeRunStartup() {

  // Save & reLoad the program preferences
  slotSaveUpdaterPrefs();
  QSettings settings("PCBSD");
  settings.setValue( "/PC-BSD/SystemUpdater/runAtStartup", runAction->isChecked() );
  loadUpdaterPrefs();

}

void UpdaterTray::slotChangePopup() {

  // Save & reLoad the program preferences
  QSettings settings("PCBSD");
  settings.setValue( "/PC-BSD/SystemUpdater/displayPopup", popAction->isChecked() );
  loadUpdaterPrefs();

}

void UpdaterTray::slotScheduledSystemCheck() {

   
   // Set our timer to check again in 24 hours.
   if ( checkSysUpdatesFrequently == TRUE)
     sysTimer->start( updateFrequency ); 

   slotStartUpdateCheck();
}




void UpdaterTray::slotStartUpdateCheck()
{
  QString SysUrl;
  QString PatchSet;
  QString Version;
  QString Arch, line;

  // Block here if check already taking place
  if ( doingCheck )
     return;
  doingCheck=true;

  // Check if the system is doing updates right now, if it is, ignore the request
  if ( programstatus == SYSTEM_CHECKING4UPDATES)
    return;

  // Set the tray icon that we are checking for updates
  QIcon Icon;
  Icon.addFile(":/images/working.png");
  trayIcon->setIcon(Icon);

  // Set the status to checking for system updates
  programstatus = SYSTEM_CHECKING4UPDATES;
  wardenstatus = WARDEN_UP2DATE;

  // Create the tooltip popup now
  displayTooltip();

  qDebug() << "Starting System Update Check";

  //Check for installed system updates needing computer to restart to finish
  bool needRestart = false;
  if( QFile::exists("/usr/local/tmp/update-stagedir/doupdate.sh") ){
    needRestart = true;
  }

  bool haveUp = false;
  bool haveNGUp = false;
  bool haveUpWarden = false;
  
  // Check for PC-BSD updates
  /////////////////////////////////////////////
  QProcess p;
  p.start(QString("sudo"), QStringList() << "pc-updatemanager" << "check");
  while(p.state() == QProcess::Starting || p.state() == QProcess::Running) {
     p.waitForFinished(200);
     QCoreApplication::processEvents();
  }

  while (p.canReadLine()) {
    line = p.readLine().simplified();
    if ( line.indexOf("The following updates are available") == 0 ) {
      haveUp = true;
      break;
    } 
  }
  /////////////////////////////////////////////

  // Check for meta-pkg updates
  /////////////////////////////////////////////
  QProcess m;
  m.start(QString("sudo"), QStringList() << "pc-updatemanager" << "pkgcheck");
  while(m.state() == QProcess::Starting || m.state() == QProcess::Running) {
     m.waitForFinished(200);
     QCoreApplication::processEvents();
  }

  while (m.canReadLine()) {
    line = m.readLine().simplified();
    if ( line.indexOf("To start the upgrade") == 0 ) {
      haveNGUp = true;
      break;
    } 
  }
  /////////////////////////////////////////////

  // Now check if there are freebsd-updates to install
  /////////////////////////////////////////////
  QProcess f;
  f.start(QString("sudo"), QStringList() << "pc-fbsdupdatecheck");
  while(f.state() == QProcess::Starting || f.state() == QProcess::Running) {
     f.waitForFinished(200);
     QCoreApplication::processEvents();
  }
  while (f.canReadLine()) {
    line = f.readLine().simplified();
    if ( line.indexOf("The following files will be updated ") == 0) {
       haveUp = true;
       break;
    }
  }
  /////////////////////////////////////////////

  // Check for warden updates
  /////////////////////////////////////////////
  QProcess w;
  w.start(QString("warden"), QStringList() << "checkup" << "all");
  while(w.state() == QProcess::Starting || w.state() == QProcess::Running) {
     w.waitForFinished(200);
     QCoreApplication::processEvents();
  }

  while (w.canReadLine()) {
    line = w.readLine().simplified();
    if ( line.indexOf("The following package updates") == 0 ) {
      haveUpWarden = true;
      wardenstatus = WARDEN_UPDATE_AVAIL;
      break;
    } 
    if ( line.indexOf("The following files will be updates ") == 0 ) {
      haveUpWarden = true;
      wardenstatus = WARDEN_UPDATE_AVAIL;
      break;
    } 
  }
  /////////////////////////////////////////////

  if ( needRestart ) {
    programstatus = SYSTEM_RESTART_NEEDED;
  } else if ( haveUp ) {
    programstatus = SYSTEM_UPDATE_AVAIL;
  } else if ( haveNGUp ) {
    programstatus = PACKAGE_UPDATE_AVAIL;
  } else {
    // We have no updates available, indicate that now.
    programstatus = SYSTEM_UP2DATE;
  }
  
  //Display the info based on the program status
  contextMenuRefresh();
  displayTooltip();

  // Ready to check again later
  doingCheck=false;

}

/* This function is called to repaint the context menu based on program status */
void UpdaterTray::contextMenuRefresh() {

  QIcon Icon;

  // If the program is completely updated!
  if ( programstatus == SYSTEM_UP2DATE ) 
     Icon.addFile(":/images/updated.png");

  // If the program couldn't get a list of updates from the server
  if ( programstatus == CHECK_FAILED ) 
     Icon.addFile(":/images/connecterror.png");

  // If the program has a PBI update
  if ( pbistatus == PBI_UPDATES_AVAIL ) 
     Icon.addFile(":/images/pbiupdates.png");

  /* Start checking the system - updater status now, which superceedes the PBI stuff */

  // If the program is checking updates right now
  if ( programstatus == SYSTEM_CHECKING4UPDATES ) 
     Icon.addFile(":/images/working.png");

  if ( programstatus == PACKAGE_UPDATE_AVAIL ) 
     Icon.addFile(":/images/pkgupdates.png");

  // If the program shows system updates available
  if ( programstatus == SYSTEM_UPDATE_AVAIL || wardenstatus == WARDEN_UPDATE_AVAIL ) 
     Icon.addFile(":/images/sysupdates.png");
  
  if ( programstatus == SYSTEM_UPDATING)
     Icon.addFile(":/images/updating.png");

  if ( programstatus == SYSTEM_RESTART_NEEDED)
     Icon.addFile(":/images/restart.png");
  
  // Check if we are downloading a system update in the background
  if ( autoStatus == AUTODOWNLOAD)
     Icon.addFile(":/images/go-down.png");

  // Set the status that we are installing updates
  if ( autoStatus == AUTOINSTALLING)
     Icon.addFile(":/images/updating.png");

  // Check if we are downloading a system update in the background
  if ( autoStatus == AUTOFAILED)
     Icon.addFile(":/images/connecterror.png");

  trayIcon->setIcon(Icon);

  // Update the tooltip to what we are doing now
  displayTooltip();
 
}


void UpdaterTray::slotOpenConfigDlg() {
}


QString UpdaterTray::getLineFromCommandOutput( QString command )
{
	FILE *file = popen(command.toLatin1(),"r"); 
	
	char buffer[100];
	
	QString line = ""; 
	char firstChar;
	
	if ((firstChar = fgetc(file)) != -1){
		line += firstChar;
		line += fgets(buffer,100,file);
	}
	
	
	pclose(file);
	
	
	return line;
}


void UpdaterTray::slotTrayActivated(QSystemTrayIcon::ActivationReason reason) {
   if(reason == QSystemTrayIcon::Context)
     return;

   if(reason == QSystemTrayIcon::Trigger) {
       if ( (programstatus != SYSTEM_UPDATE_AVAIL && programstatus != PACKAGE_UPDATE_AVAIL) && (pbistatus == PBI_UPDATES_AVAIL) )
       {
            slotOpenSoftwareManagerInstalled();
	    return;
       }
       else
       {
	   if ( programstatus == SYSTEM_UPDATE_AVAIL ) {
              slotOpenUpdateManager();
	      return;
	   }
	   if ( programstatus == PACKAGE_UPDATE_AVAIL ) {
              slotOpenPackageManager();
	      return;
	   }
	   if ( wardenstatus == WARDEN_UPDATE_AVAIL ) {
              slotOpenJailManager();
	      return;
	   }
       }//else
   }

   slotOpenSoftwareManager();
}

void UpdaterTray::slotOpenJailManager(void)
{   
   system ("(pc-su warden gui) &"); 
}

void UpdaterTray::slotOpenPackageManager(void)
{   
   system ("(pc-su pc-pkgmanager) &"); 
}

void UpdaterTray::slotOpenUpdateManager(void)
{   
   if ( programstatus == SYSTEM_RESTART_NEEDED ){
     qDebug() << "System restart required: Do not open update manager";
     QMessageBox::warning(this, tr("Restart Required"), tr("Please restart your computer to complete the pending updates") );
   }else{
     system ("(sudo pc-updategui) &"); 
   }
}

void UpdaterTray::slotOpenSoftwareManager(void)
{   
    system ("(pc-softwaremanager) &"); 
}

void UpdaterTray::slotOpenSoftwareManagerInstalled(void)
{   
    system ("(pc-softwaremanager -installed) &"); 
}

// The user wants to quit the application
void UpdaterTray::slotQuitTray() {
 exit(0);
}



void UpdaterTray::displayTooltip() {

   QString  tooltipStr;

   tooltipStr = QString(tr("PC-BSD Update Manager"));

   tooltipStr += "<br><hr>";

   if (programstatus == SYSTEM_UP2DATE && wardenstatus == WARDEN_UP2DATE ) {
      tooltipStr += "<br>" + tr("Your system is fully updated");
   }
   
   if (programstatus == CHECK_FAILED ) {
      tooltipStr += "<br>" + tr("Failed to contact the update server!");
   }

   if (programstatus == SYSTEM_CHECKING4UPDATES ) {
      tooltipStr += "<br>" + tr("Checking for updates...");
   }
   
   if (programstatus == SYSTEM_UPDATE_AVAIL) {
      tooltipStr += "<br>" + tr("System updates available");
      if ( !shownPopup && popAction->isChecked() ) {
	shownPopup=true;
        QTimer::singleShot(15000, this, SLOT(slotShowSysUpdatePopup()));
      }
   }

   if (programstatus == PACKAGE_UPDATE_AVAIL) {
      tooltipStr += "<br>" + tr("Package updates available");
      if ( !shownPopup && popAction->isChecked() ) {
	shownPopup=true;
        QTimer::singleShot(15000, this, SLOT(slotShowPkgUpdatePopup()));
      }
   }

   if (wardenstatus == WARDEN_UPDATE_AVAIL) {
      tooltipStr += "<br>" + tr("Jail updates available");
      if ( !shownPopup && popAction->isChecked() ) {
	shownPopup=true;
        QTimer::singleShot(15000, this, SLOT(slotShowJailUpdatePopup()));
      }
   }
   
   if (programstatus == SYSTEM_RESTART_NEEDED) {
     tooltipStr += "<br>" + tr("System restart required to finish updates");
   }

   // If the program has a PBI update
   if ( pbistatus == PBI_UPDATES_AVAIL ) 
   {
      tooltipStr += "<br><br>" + tr("PBI updates available") + "<br><br>";
      tooltipStr += updatePBITextList;
      if ( !shownPopup && popAction->isChecked() ) {
	shownPopup=true;
        QTimer::singleShot(15000, this, SLOT(slotShowPBIUpdatePopup()));
      }
   }
   
   trayIcon->setToolTip(tooltipStr);

}

void UpdaterTray::slotSaveUpdaterPrefs() {
  if (runAction->isChecked() )
    system("pbreg set /PC-BSD/SystemUpdater/runAtStartup true");
  else
    system("pbreg set /PC-BSD/SystemUpdater/runAtStartup false");


}

void UpdaterTray::loadUpdaterPrefs() {
  // Load the user preferences for the System Updater
  QSettings settings("PCBSD");
  
  bool enableStartup = settings.value( "/PC-BSD/SystemUpdater/runAtStartup", TRUE ).toBool();
  runAction->setChecked( enableStartup );

  bool enablePopupNotify = settings.value( "/PC-BSD/SystemUpdater/displayPopup", TRUE ).toBool();
  popAction->setChecked( enablePopupNotify );

  // Load a custom tmpdir
  customTmpDir = PATCHTMPDIR_DEFAULT;
  customTmpDir = settings.value("/PC-BSD/SystemUpdater/customTmpDir", customTmpDir).toString();

  // Load a custom tmpdir
  updateFrequency = UPDATE_MSEC;
  updateFrequency = settings.value("/PC-BSD/SystemUpdater/checkUpdateFrequency", updateFrequency).toLongLong();

}

void UpdaterTray::slotSysUpdateTimer() {
  slotScheduledSystemCheck();
}




/**********************************************************************
Code for the PBI update checking functionality of the system updater
***********************************************************************/

void UpdaterTray::slotScheduledPBICheck()
{
   // Set our timer to check again in 24 hours.
   if ( checkPBIUpdatesFrequently == TRUE)
     pbiTimer->start( updateFrequency); 

   slotStartPBIUpdateCheck();

}

void UpdaterTray::slotStartPBIUpdateCheck()
{

  if ( pbistatus == PBI_CHECKING4UPDATES || pbistatus == PBI_UPDATING )
     return;

  qDebug() << "Starting PBI update check...";

  slotPBICheckUpdate();

} 

// Start checking our list of PBIs which may be updatable
void UpdaterTray::slotPBICheckUpdate()
{
  // Empty out our PBI buffer
  PBIBuffer="";
  updatePBITextList = "";

  qDebug() << "Checking For PBI Updates...";
  checkPBIProc = new QProcess();
  checkPBIProc->setProcessChannelMode(QProcess::MergedChannels);
  connect(checkPBIProc, SIGNAL(readyReadStandardOutput()), this, SLOT(slotReadPBIBuffer()));
  connect(checkPBIProc, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(slotPopulatePBIList()) );
  checkPBIProc->start(QString("pbi_update"), QStringList() << "--check-all" );
}

void UpdaterTray::slotReadPBIBuffer()
{
    while( checkPBIProc->canReadLine() )
	PBIBuffer += checkPBIProc->readLine();
}

// The update checking is finished
void UpdaterTray::slotPopulatePBIList()
{
	if ( PBIBuffer.indexOf("- Available:") != -1 ) {
  	    	updatePBITextList = PBIBuffer;
    		pbistatus = PBI_UPDATES_AVAIL;
    	} else {
     		// No PBI updates found right now!
     		pbistatus = PBI_UPDATED;
  	}

	contextMenuRefresh();
}

void UpdaterTray::slotShowPkgUpdatePopup()
{
        disconnect(trayIcon, SIGNAL(messageClicked()), 0, 0 );
	trayIcon->showMessage(tr("Package Updates Available"), tr("Important package updates are available. Click here to install them!"), QSystemTrayIcon::Critical);
        connect( trayIcon, SIGNAL(messageClicked()), this, SLOT(slotOpenPackageManager()) );
}


void UpdaterTray::slotShowSysUpdatePopup()
{
        disconnect(trayIcon, SIGNAL(messageClicked()), 0, 0 );
	trayIcon->showMessage(tr("System Updates Available"), tr("Important system updates are available. Click here to install them!"), QSystemTrayIcon::Critical);
        connect( trayIcon, SIGNAL(messageClicked()), this, SLOT(slotOpenUpdateManager()) );
}

void UpdaterTray::slotShowJailUpdatePopup()
{
        disconnect(trayIcon, SIGNAL(messageClicked()), 0, 0 );
	trayIcon->showMessage(tr("Jail Updates Available"), tr("Important jail updates are available. Click here to launch the Warden!"), QSystemTrayIcon::Critical);
        connect( trayIcon, SIGNAL(messageClicked()), this, SLOT(slotOpenJailManager()) );
}

void UpdaterTray::slotShowPBIUpdatePopup()
{
        disconnect(trayIcon, SIGNAL(messageClicked()), 0, 0 );
	trayIcon->showMessage(tr("Software Updates Available"), tr("Software updates are available. Click here to install them!"), QSystemTrayIcon::Warning);
        connect( trayIcon, SIGNAL(messageClicked()), this, SLOT(slotOpenSoftwareManagerInstalled()) );
}

void UpdaterTray::slotSingleInstance()
{
  trayIcon->show();
}
