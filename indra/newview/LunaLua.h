#ifndef SHLLUA_H
#define SHLLUA_H
/**
 * @file FLLua.h
 * @brief FlexLife Viewer Lua Integration Framework
 * @author N3X15
 *
 *  Copyright (C) 2008-2009 FlexLife Contributors
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#include "llviewerprecompiledheaders.h"

#include <string>
#include <queue>
#include <vector>

extern "C" {
	#include "lua.h"
	#include "lauxlib.h"
	#include "lualib.h"
}

#include "llstring.h"
#include "message.h"
#include "llviewerimage.h"
#include "llviewerregion.h"
#include "llviewerobject.h"

//Macros for Lua event calls
//No arguments. LUA_CALL0("OnSomething");
//Arguments.	LUA_CALL("OnSomething") << arg1 << arg2 << ... << LUA_END;
class lua_done {};
#define LUA_CALL0(name) do{(new HookRequest(name))->Send();}while(0)
#define LUA_CALL(name) do{(*(new HookRequest(name))) 
#define LUA_END (lua_done*)0;}while(0)

class HookRequest
{
public:
	//These overrides create a simple shift method for adding arguments painlessly.
	//If unhandled type use add.
	HookRequest& operator<<(const int &in);
	HookRequest& operator<<(const float &in);
	HookRequest& operator<<(const std::string &in);
	HookRequest& operator<<(const char *in);
	HookRequest& operator<<(const LLUUID &in);
	HookRequest& operator<<(LLViewerRegion *in);
	HookRequest& operator<<(LLViewerObject *in);
	HookRequest& operator<<(const lua_done *in)
		{Send(); return *this;}//Send off.

	HookRequest(const char *Name) { mName=Name; }
	HookRequest(const std::string& Name) { mName=Name; }

	void add(const std::string &in)
		{mArgs.push_back(in);}
	const char* getName() const
		{return mName.c_str(); }
	const char* getArg(unsigned idx) const
		{return (idx >= 0 && idx < mArgs.size()) ? mArgs[idx].c_str() : NULL;}
	int getNumArgs() const
		{return mArgs.size(); };

	void Send();
private:
	std::vector<std::string> mArgs;
	std::string mName;
};

struct CB_Base;
class FLLua : public LLThread
{
friend class HookRequest;
friend struct CriticalSection;
	//LOG_CLASS(FLLua);
public:
	FLLua();
	~FLLua();

	static bool init();
	static void cleanupClass();

	static FLLua* getInstance();

	static void callCommand(const std::string &cmd);

	//Injection of events into MAIN thread. Needed for gl calls. Render context does NOT carry over threads.
		//Called from lua thread
	static void regClientEvent(CB_Base *entry);
		//Called from MAIN thread
	static void execClientEvents();
	
	/*
	The concept of CriticalSections in this implementation is to ensure thread safe shared memory accessing.
	When a CriticalSection() object is created it will pause the lua thread until the MAIN thread signals it's
	safe to access this memory. The MAIN thread allows a 'safe' window of up to 25ms for lua to access shared
	memory while it tries to finish its loop.
	If lua exceeds this window the MAIN thread will force itself to resume and the next CriticalSection request
	by the lua thread will make the lua thread pause itself until MAIN signals once more.
	
	The CriticalSection structure simply facilitates keeping an accurate tally on CriticalSection accesses.
	This tally is useful in determining if the MAIN thread is resuming while lua is still accessing shared
	memory, which is not a thread safe condition. 
	This scenario only happens when the lua thread exceeds the 'safe' window.
	*/

	struct CriticalSection
	{
		bool entered;
		CriticalSection(){entered=FLLua::setCriticalSection(true);}
		~CriticalSection(){if(entered)FLLua::setCriticalSection(false);}
	};
	LLAtomic32<int> mCriticalSections;
	//	Called from lua thread
	static bool setCriticalSection(bool enter); //use FLLua::CriticalSection() instead.
private:
	//	Called from lua thread
//	static bool setCriticalSection(bool enter); //use FLLua::CriticalSection() instead.

	bool load(); //pulled out of run so we can determine if load failed immediately.

#ifdef _WITH_CEGUI
	// UI STUFF BEGINS HERE (Separating it from this class fails miserably for some reason)
	void initUI(lua_State *L);
	static void render();
#endif
	void run();
	
	bool LoadFile(std::string file);
	void RunMacro(const std::string &what);
	void RunString(std::string &s);
	void ExecuteHook(HookRequest *hook);

	static bool isMacro(const std::string &what);
	static void callLuaHook(HookRequest *hook); //Called via HookRequest::Send()
	
	//Set in init:
	// Object instance
	static FLLua *sInstance;
	// Lua stack
	lua_State *pLuaStack; 
	// Is CallHook present.
	bool listening;

	//Must mutex to access:
	// Outbound queued hooks
	std::queue<HookRequest*> mQueuedHooks;
	// Outbound queued commands
	std::queue<std::string> mQueuedCommands;
	// Inbound queued events
#ifdef FL_PRI_EVENTS
	std::priority_queue<CB_Base*, std::vector<CB_Base*>,std::greater<std::vector<CB_Base*>::value_type> > mQueuedEvents;
#else
	std::queue<CB_Base*> mQueuedEvents;
#endif
	//Mutex free:
	// Read in both loops
	bool mAllowPause;	
	//LLAtomic32<bool> mError;	//Not used right now
	// Read in lua loop
	LLAtomic32<bool> mPendingHooks;		//!mQueuedHooks.isEmpty()
	LLAtomic32<bool> mPendingCommands;	//!mQueuedCommands.isEmpty()
	// Read in MAIN loop
	LLAtomic32<bool> mPendingEvents;	//!mQueuedEvents.isEmpty()
	
};

/*Ugly hacky code follows. Kludgy workaround for making SWIG (somewhat) thread-safe.

	Client callback classes. Checked and executed every frame.
	DONT USE POINTERS WITH THESE. These classes rely on static 
	allocation of passed arguments to avoid destruction of them 
	before next frame.
	Make sure passed variables are NON-VOLATILE.

	Usage:
		CB_Args2<std::string,double>(setQueuedParams,paramname,1);

	Now, why is this needed at all? Some functions exposed to Lua will not be able to 
	safely be ran from the lua thread. 
	For instance LLCharacter::setVisualParamWeight will crash. It must be called from
	MAIN due to eventually calling glGenTextures. Opengl render context doesn't carry over
	to the lua thread, therefore opengl will fail to assign a texture index.

PLEASE DISCUSS ALTERNATIVES TO THIS HORRIBLE MESS @ http://www.nexisonline.net/forums/index.php?/topic/14-flexible-lua-main-thread-queue/

*/

struct CB_Base //If you don't want to bother with the odd templates below, just derive from this manually.
{
	static CB_Base *sent;
	int pri;
	virtual void OnCall(){}; //overridden by derived classes. Called by main thread on next frame.
	virtual CB_Base *clone(){return this;}//overridden by derived classes. Default must be dynamic.
	CB_Base(int _pri=5){pri=_pri;if(sent!=this)FLLua::regClientEvent(sent=clone());}; //Always called
};
struct CB_Dummy_Args0 : public CB_Base
{
	typedef void (*CB_FN)();
	virtual void OnCall()	{fn();}
	virtual CB_Base *clone(){return new CB_Dummy_Args0(*this);} //Pass a dynamically allocated copy to the queue
	CB_FN fn;
	CB_Dummy_Args0(CB_FN _fn, int _pri=5) : fn(_fn),CB_Base(_pri){}
};
// Compiler error C2530: refrences must be initialized. Hurf durf.
inline void CB_Args0(void (*CB_FN)(),int _pri=5){new CB_Dummy_Args0(CB_FN,_pri);}

template <typename T1>
struct CB_Args1 : public CB_Base
{
	typedef void (*CB_FN)(T1& _t1);
	virtual void OnCall()	{fn(t1);}
	virtual CB_Base *clone(){return new CB_Args1(*this);}
	CB_FN fn;
	T1 t1;
	CB_Args1(CB_FN _fn,const T1 &_t1, int _pri=5) : fn(_fn), t1(_t1), CB_Base(_pri){}
};
template <typename T1,typename T2>
struct CB_Args2 : public CB_Base
{
	typedef void (*CB_FN)(T1 &_t1,T2 &_t2);
	virtual void OnCall()	{fn(t1,t2);}
	virtual CB_Base *clone(){return new CB_Args2(*this);}
	CB_FN fn;
	T1 t1;	T2 t2;
	CB_Args2(CB_FN _fn,const T1 &_t1,const T2 &_t2, int _pri=5) : fn(_fn), t1(_t1), t2(_t2), CB_Base(_pri){}
};
template <typename T1,typename T2,typename T3>
struct CB_Args3 : public CB_Base
{
	typedef void (*CB_FN)(T1 &_t1,T2 &_t2,T3 &_t3);
	virtual void OnCall()	{fn(t1,t2,t3);}
	virtual CB_Base *clone(){return new CB_Args3(*this);}
	CB_FN fn;
	T1 t1;	T2 t2;	T3 t3;
	CB_Args3(CB_FN _fn,const T1 &_t1,const T2 &_t2,const T3 &_t3, int _pri=5) : fn(_fn), t1(_t1), t2(_t2), t3(_t3), CB_Base(_pri){}
};
template <typename T1,typename T2,typename T3,typename T4>
struct CB_Args4 : public CB_Base
{
	typedef void (*CB_FN)(T1 &_t1,T2 &_t2,T3 &_t3,T4 &_t4);
	virtual void OnCall()	{fn(t1,t2,t3,t3,t4);}
	virtual CB_Base *clone(){return new CB_Args4(*this);}
	CB_FN fn;
	T1 t1;	T2 t2;	T3 t3;  T4 t4;
	CB_Args4(CB_FN _fn,const T1 &_t1,const T2 &_t2,const T3 &_t3, const T4 &_t4, int _pri=5) : fn(_fn), t1(_t1), t2(_t2), t3(_t3), t4(_t4), CB_Base(_pri){}
};
//etc...

int luaOnPanic(lua_State *L);
std::string Lua_getErrorMessage(lua_State *L);


#endif
