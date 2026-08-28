/* @@@LICENSE
*
*      Copyright (c) 2009-2012 Hewlett-Packard Development Company, L.P.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* LICENSE@@@ */




#include "Common.h"

#include "IpcServer.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "HostBase.h"
#include "IpcClientHost.h"
#include "WebAppMgrProxy.h"
#include "Settings.h"
#include "WebAppManager.h"
#include "WindowServer.h"
#include "ApplicationManager.h"
#include "SystemService.h"
#include "MemoryMonitor.h"
#include "CpuAffinity.h"

#include "helpers/HApp.h"

#ifndef TARGET_DESKTOP	
	#include "rolegen.h"
#endif

// the prefix that comes before the appid in all app paths
const char *APP_PREFIX = "/media/cryptofs/apps/usr/palm/applications/";

// Remembers an environment variable so it can be put back after it has been set
// for a child's benefit. Restoring on destruction too, so an early return out of
// a launch cannot leave the launched app's loader settings behind in our own
// environment for the next child to inherit.
class EnvVarSaver
{
public:
	EnvVarSaver(const char * name)
		: m_name(name)
		, m_restored(false)
	{
		const char * value = ::getenv(name);
		m_wasSet = (value != 0);
		if (m_wasSet)
			m_value = value;
	}

	~EnvVarSaver()
	{
		restore();
	}

	void restore()
	{
		if (m_restored)
			return;
		m_restored = true;
		if (m_wasSet)
			::setenv(m_name, m_value.c_str(), 1);
		else
			::unsetenv(m_name);
	}

private:
	const char * m_name;
	std::string m_value;
	bool m_wasSet;
	bool m_restored;
};


static int kNukeProcessTimeoutMs = 2000;

static int PrvForkFunction(void* data) __attribute__((unused));
static pid_t PrvLaunchProcess(char* const args[]);

IpcServer* IpcServer::instance()
{
	static IpcServer* s_server = 0;
	if (G_UNLIKELY(s_server == 0)) {
		s_server = new IpcServer;
	}

	return s_server;    
}

IpcServer::IpcServer()
	: PIpcServer("sysmgr", HostBase::instance()->mainLoop())
	, m_nukeProcessTimer(HostBase::instance()->masterTimer(), this,
						 &IpcServer::nukeProcessTimer)
{
	static const int kGroupId = 1000;  // group "luna"

	// flash needs this:
	chmod("/tmp/pipcserver.sysmgr", S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
	chown((char*)"/tmp/pipcserver.sysmgr", 0, kGroupId);
}


IpcServer::~IpcServer()
{
}

void IpcServer::clientConnected(int pid, const std::string& name, PIpcChannel* channel)
{
	// Find the appid from the path of the file
	std::string appId = appIdFromPid(pid);
	if (appId.empty())
		appId = name;
	
    g_message("%s (%d): Client connected %s (%d)", __PRETTY_FUNCTION__, __LINE__,
			  appId.c_str(), pid);

	// Update/Add the new pid into the processMap
	ProcessMap::iterator it = m_nativeProcessMap.find(appId);
	if (it != m_nativeProcessMap.end()){
		if(pid != it->second)
		{
			// update the memory watcher for this process, in case there is one
			MemoryMonitor::instance()->monitorNativeProcessMemory(pid, 0, it->second);
		}
		m_nativeProcessMap.erase(it);
	}

	m_nativeProcessMap[appId] = pid;
	
	if (0 != strcmp(name.c_str(), WEB_APP_MGR_IPC_NAME))
	{ // regular (native) app connecting
		IpcClientHost* c = new IpcClientHost(pid, appId, channel);
		m_clientHostSet.insert(c);
	} else {
		// WebKit WebApp Manager connecting
		WebAppMgrProxy* c = WebAppMgrProxy::connectWebAppMgr(pid, channel);
		m_clientHostSet.insert(c);
	}
	
}

/*
int IpcServer::launchNativeProcess(const std::string& appId, const char* path, char* const argv[], int requiredMemory)
{
	// default to "game" type
	return launchNativeProcess(appId,path,argv,"game",requiredMemory);
}
*/

int IpcServer::launchNativeProcess(const std::string& appId, const char* path, char* const argv[], ApplicationDescription::Type appType, int requiredMemory)
{
	// figure out the jailer to use for this app type
	const char *jailerType = "web"; // default is "web" type
	if ( appType == ApplicationDescription::Type_Native )
	{
		// native apps get "game"
		jailerType = "game";
	}
	else if ( appType == ApplicationDescription::Type_PDK 
           || appType == ApplicationDescription::Type_Qt )
	{
		// pdk apps get "pdk"
		jailerType = "pdk";
	}
	
	ProcessMap::const_iterator it = m_nativeProcessMap.find(appId);
	if (it != m_nativeProcessMap.end()) {
		g_debug("%s: Process %s (%s) already running. Not launching",
				__PRETTY_FUNCTION__, appId.c_str(), path);

		IpcClientHost* host = clientHostForAppId(appId);
		if (host)
			host->relaunch();
		
		return it->second;
	}
	
	if(!MemoryMonitor::instance()->allowNewNativeAppLaunch(requiredMemory)){ // not enough memory...
		g_warning("%s: Low memory condition Not allowing native app for appId: %s. RequiredMemory = %d",
				  __PRETTY_FUNCTION__, appId.c_str(), requiredMemory);
		WindowServer::instance()->appLaunchPreventedUnderLowMemory();
		WebAppMgrProxy::instance()->performLowMemoryActions(); // try to free up some memory and notify the user to close cards
		return 0; // return 0 for low memory failure
	}
	
	// role generation is not supported in desktop
#ifndef TARGET_DESKTOP	
	// push a role file. This will allow the native app to make
	// luna-service calls. This call is fast, and safe to call when the role file
	// already exists, so we just call it every launch.
	pdkGenerateRole(appId, path);
#endif

	// assemble the working path
	char workingPath[PATH_MAX];
	snprintf(workingPath, PATH_MAX, "%s%s", APP_PREFIX, appId.c_str());

	char* const minArgs[] = { (char*) path, NULL };
	char* const* args = argv;
	if (!args)
		args = minArgs;
	
	// build new arg list with stuff for jailer
	// first count existing args
	int i,e_argc;
	i = 0;
	for (e_argc = 0; args[e_argc]; e_argc++ );
	const char *newargs[e_argc + 14];

	// get the base file name
	char baseFile[PATH_MAX];
	HApp_GetBaseFile(path, baseFile, PATH_MAX);
	
	newargs[i++] = "/usr/sbin/setcpushares-pdk"; // the cpu share manager
	newargs[i++] = "-a";
	newargs[i++] = appId.c_str();
    newargs[i++] = "-b";
    newargs[i++] = baseFile;
	newargs[i++] = "/usr/bin/jailer";
	newargs[i++] = "-t";
	newargs[i++] = jailerType;
	newargs[i++] = "-i";
	newargs[i++] = appId.c_str();
	newargs[i++] = "-p";
	newargs[i++] = workingPath;
	newargs[i++] = path;
	for(e_argc = 0; args[e_argc]; e_argc++) 
		newargs[i++] = args[e_argc];
	newargs[i] = NULL;
	g_message("%s: Process %s launching in jail with args %s %s %s %s %s %s",
		__PRETTY_FUNCTION__,appId.c_str(),newargs[0],newargs[1],
		newargs[2],newargs[3],newargs[4],newargs[5]);
	
	
	// These are the launched app's loader settings, not ours; they go into our own
	// environment only because the child inherits it across the fork. So take a copy
	// of what was here and put it back the moment the child is on its way - whatever
	// is left set here is inherited by every OTHER process sysmgr spawns from now on.
	// LD_PRELOAD leaking was the worst of it: libpvrtc.so needs libnapp's symbols,
	// which only a PDK app links, and sysmgr runs with LD_BIND_NOW=1, so the loader
	// resolves it eagerly and kills anything else that inherits it - "symbol lookup
	// error: /usr/lib/libpvrtc.so: undefined symbol: NApp_GetPortabilityValue", exit
	// 127. After one PDK app launch that was every sh, rm and ipkg sysmgr ran, which
	// is why removing an app stopped working (FAILED_IPKG_REMOVE) until a restart.
	EnvVarSaver savedLibraryPath("LD_LIBRARY_PATH");
	EnvVarSaver savedPreload("LD_PRELOAD");
	EnvVarSaver savedQpaPlatform("QT_QPA_PLATFORM");
	EnvVarSaver savedQmlImportPath("QML_IMPORT_PATH");

	// set the lib path value
    setenv("LD_LIBRARY_PATH", workingPath, 1);

    // for PVR texture decompression
    if ( appType != ApplicationDescription::Type_Qt ) {
        setenv("LD_PRELOAD", "libpvrtc.so", 1);
    }
    else {
        setenv("QT_QPA_PLATFORM", "webos-offscreen", 1);
        setenv("QML_IMPORT_PATH", "/usr/plugins/imports", 1);
    }

    // fire off the process (jailer will preserve the lib path value in the new environment)
	pid_t pid = PrvLaunchProcess((char **)newargs);

	// the child has its copy; hand our environment back to whatever comes next
	savedLibraryPath.restore();
	savedPreload.restore();
	savedQpaPlatform.restore();
	savedQmlImportPath.restore();
	
	if (pid < 0) {
		g_critical("%s:%d failed to fork: %s", __PRETTY_FUNCTION__, __LINE__,
				   strerror(errno));
		return -1; // return -1 for launch error
	}
	else if (requiredMemory > 0)
	{
		// add a memory watch for this native app
		MemoryMonitor::instance()->monitorNativeProcessMemory(pid, requiredMemory);
	}

	m_nativeProcessMap[appId] = pid;
	g_message("%s: Process %s (%s) launched with pid: %d", __PRETTY_FUNCTION__,
			  appId.c_str(), path, pid);

	g_child_watch_add_full(G_PRIORITY_HIGH, pid, IpcServer::childProcessDiedCallback,
						   this, NULL);
	
	return pid;  
}

int IpcServer::launchWebAppProcess(const std::string& appId, const char* url, const char** argv)
{
    // Not implemented
	return 0;
}

void IpcServer::suspendProcess(int pid)
{
    ::kill(pid, SIGSTOP);
}

void IpcServer::resumeProcess(int pid)
{
    ::kill(pid, SIGCONT);
}

void IpcServer::killProcess(int pid, bool notifyUser)
{
	g_warning("%s: killing process: %d", __PRETTY_FUNCTION__, pid);
	
	if(notifyUser) {
		std::string nullString;
		std::string appId = appIdFromPid(pid);
		std::string appName("Application");
		std::string appTitle;
		
		if(!appId.empty()){
			ApplicationDescription*  desc = ApplicationManager::instance()->getAppById(appId);
			if(desc){
				appName = desc->menuName();
				if (desc->getDefaultLaunchPoint()){
					appTitle = desc->getDefaultLaunchPoint()->title();			
				}
			}
		}
		
		SystemService::instance()->postApplicationHasBeenTerminated(appTitle, appName, appId);
	}
	
	::kill(pid, SIGKILL);
	processRemoved(pid, false);
}
		
void IpcServer::processRemoved(int pid, bool doCleanup)
{
	if (doCleanup)
		::waitid(P_PID, pid, NULL, WEXITED | WNOHANG);	

	for (ProcessMap::iterator it = m_nativeProcessMap.begin();
		 it != m_nativeProcessMap.end(); ++it) {

		if (it->second == pid) {
			g_message("%s: pid: %d, appId: %s:", __PRETTY_FUNCTION__,
					  pid, it->first.c_str());
			m_nativeProcessMap.erase(it);
			return;
		}
	}

	for (ProcessMap::iterator it = m_webAppProcessMap.begin();
		 it != m_webAppProcessMap.end(); ++it) {

		if (it->second == pid) {
			g_message("%s: pid: %d, appId: %s:", __PRETTY_FUNCTION__,
					  pid, it->first.c_str());
			m_webAppProcessMap.erase(it);
			return;
		}
	}
}

void IpcServer::addProcessToNukeList(int pid)
{
	m_nukeSet.insert(pid);
	
	m_nukeProcessTimer.stop();
	m_nukeProcessTimer.start(kNukeProcessTimeoutMs, true);
}

bool IpcServer::nukeProcessTimer()
{
	for (ProcessSet::const_iterator it = m_nukeSet.begin();
		 it != m_nukeSet.end(); ++it) {
		killProcess(*it);
	}

	m_nukeSet.clear();
	
	return false;    
}

std::string IpcServer::appIdFromPid(int pid)
{
	const int len = 256;
	gchar buf[len];
	gchar  *exeName;
	GError *error = 0;
	
	snprintf(buf, len-1, "/proc/%d/exe", pid);
	exeName = g_file_read_link(buf, &error);

	if (error)
	{
		g_critical("IpcServer::appIdFromPid failed: %s", error->message);
		g_error_free(error);
	}

	
	if (NULL != exeName) {
		snprintf(buf, len-1, "%s", exeName);
		free(exeName);
	}
	else {
		return std::string();	
	}
		

	std::string appInstallRelative = Settings::LunaSettings()->appInstallRelative;	
	gchar* location = g_strrstr(buf, appInstallRelative.c_str());
	if (!location)
	{
		return std::string();
	}

	gchar* appIdStart = location + strlen(appInstallRelative.c_str());

	while (true) {
		if (*appIdStart == 0) {
			// reached end of string
			return std::string();
		}

		if (*appIdStart != '/')
			break;

		appIdStart++;
	}

	gchar* appIdEnd = appIdStart;
	while (true) {
		if (*appIdEnd == 0) {
			// reached end of string
			return std::string();
		}

		if (*appIdEnd == '/')
			break;

		appIdEnd++;
	}

	if (appIdEnd <= appIdStart)
	{
		return std::string();
	}
	
	return std::string(appIdStart, appIdEnd - appIdStart);
}

void IpcServer::ipcClientHostQuit(IpcClientHost* client)
{
	m_clientHostSet.erase(client);
	// FIXME: this prevents having multiple client hosts per process
	m_nukeSet.erase(client->pid());
	processRemoved(client->pid(), false);
}


IpcClientHost* IpcServer::clientHostForAppId(const std::string& appId) const
{
	for (ClientSet::const_iterator it = m_clientHostSet.begin();
		 it != m_clientHostSet.end(); ++it) {

		g_debug("Checking clientHost: '%s' against '%s'", (*it)->name().c_str(),
				appId.c_str());
		if ((*it)->name() == appId)
			return *it;
	}

	return 0;
}

void IpcServer::childProcessDiedCallback(GPid pid, gint status, gpointer data)
{
	IpcServer* server = (IpcServer*) data;
	server->childProcessDied(pid, status);
}

void IpcServer::childProcessDied(GPid pid, gint status)
{
	g_message("%s: pid: %d, status %d", __PRETTY_FUNCTION__, pid, status); 
	processRemoved(pid, false);
	g_spawn_close_pid(pid);
}

static int PrvForkFunction(void* data)
{
	// Move this process into the root cgroup
	int pid = getpid();

	const int maxSize = 64;
	char buf[maxSize];

	snprintf(buf, maxSize - 1, "%d", pid);
	buf[maxSize - 1] = 0;

	int fd = ::open("/dev/cgroup/tasks", O_WRONLY);
	if (fd >= 0) {
		ssize_t result = ::write(fd, buf, ::strlen(buf) + 1);
		Q_UNUSED(result);
		::close(fd);
	}
	else {
		perror("root cgroup tasks file not found");
	}

	// Reset cpu affinity before entering the jail because the jail 
	// prevents pdk apps from accessing libaffinity system files.
	resetCpuAffinity(0);

	// Now exec the process
	
	char ** argv = (char **) data;
	int ret = ::execv(argv[0], argv);
	if (ret < 0)
		perror("execv failed");

	return 0;	
}


static pid_t PrvLaunchProcess(char* const args[])
{
	pid_t pid = ::fork();
	if (pid < 0)
		return pid;

	if (pid == 0) {
		(void) PrvForkFunction((char**)args);
		exit(-1);
	}

	return pid;
}
