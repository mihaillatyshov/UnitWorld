#pragma once

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <string>
#include <thread>
#include <mutex>
#include <iostream>
#include <iosfwd>
#include <sstream>

#include "Debug/ConsoleLog.h"

using FloatingPointMicroseconds = std::chrono::duration<double, std::micro>;

struct ProfileResult
{
	std::string Name;

	FloatingPointMicroseconds Start;
	std::chrono::microseconds ElapsedTime;
	std::thread::id ThreadID;
};

struct InstrumentationSession
{
	std::string Name;
};

class Instrumentor
{
private:
	std::mutex m_Mutex;
	InstrumentationSession* m_CurrentSession;
	std::ofstream m_OutputStream;
public:
	Instrumentor()
		: m_CurrentSession(nullptr)
	{
	}

	void BeginSession(const std::string& name, const std::string& filepath = "results.json")
	{
		std::lock_guard lock(m_Mutex);
		if (m_CurrentSession) 
		{
			LOGW("Instrumentor::BeginSession('", name, "') when session '", m_CurrentSession->Name, "' already open.");
			InternalEndSession();
		}
		m_OutputStream.open(filepath);

		if (m_OutputStream.is_open()) 
		{
			m_CurrentSession = new InstrumentationSession({name});
			WriteHeader();
		} 
		else 
		{
			LOGE("Instrumentor could not open results file '", filepath, "'.");
		}
	}

	void EndSession()
	{
		std::lock_guard lock(m_Mutex);
		InternalEndSession();
	}

	void WriteProfile(const ProfileResult& result)
	{
		std::stringstream json;

		std::string name = result.Name;
		std::replace(name.begin(), name.end(), '"', '\'');

		json << std::setprecision(3) << std::fixed;
		json << ",{";
		json << "\"cat\":\"function\",";
		json << "\"dur\":" << (result.ElapsedTime.count()) << ',';
		json << "\"name\":\"" << name << "\",";
		json << "\"ph\":\"X\",";
		json << "\"pid\":0,";
		json << "\"tid\":" << result.ThreadID << ",";
		json << "\"ts\":" << result.Start.count();
		json << "}";

		std::lock_guard lock(m_Mutex);
		if (m_CurrentSession) {
			m_OutputStream << json.str();
			m_OutputStream.flush();
		}
	}

	static Instrumentor& Get() {
		static Instrumentor instance;
		return instance;
	}

private:

	void WriteHeader()
	{
		m_OutputStream << "{\"otherData\": {},\"traceEvents\":[{}";
		m_OutputStream.flush();
	}

	void WriteFooter()
	{
		m_OutputStream << "]}";
		m_OutputStream.flush();
	}

	// Note: you must already own lock on m_Mutex before
	// calling InternalEndSession()
	void InternalEndSession() {
		if (m_CurrentSession) {
			WriteFooter();
			m_OutputStream.close();
			delete m_CurrentSession;
			m_CurrentSession = nullptr;
		}
	}

};


class InstrumentationTimer
{
public:
	InstrumentationTimer(const char* name)
		: m_Name(name), m_Stopped(false)
	{
		m_StartTimepoint = std::chrono::steady_clock::now();
	}

	~InstrumentationTimer()
	{
		if (!m_Stopped)
			Stop();
	}

	void Stop()
	{
		auto endTimepoint = std::chrono::steady_clock::now();
		auto highResStart = FloatingPointMicroseconds{ m_StartTimepoint.time_since_epoch() };
		auto elapsedTime = std::chrono::time_point_cast<std::chrono::microseconds>(endTimepoint).time_since_epoch() - std::chrono::time_point_cast<std::chrono::microseconds>(m_StartTimepoint).time_since_epoch();

		Instrumentor::Get().WriteProfile({ m_Name, highResStart, elapsedTime, std::this_thread::get_id() });

		m_Stopped = true;
	}
private:
	const char* m_Name;
	std::chrono::time_point<std::chrono::steady_clock> m_StartTimepoint;
	bool m_Stopped;
};


#if defined(__GNUC__) || (defined(__MWERKS__) && (__MWERKS__ >= 0x3000)) || (defined(__ICC) && (__ICC >= 600)) || defined(__ghs__)
	#define HZ_FUNC_SIG __PRETTY_FUNCTION__
#elif defined(__DMC__) && (__DMC__ >= 0x810)
	#define HZ_FUNC_SIG __PRETTY_FUNCTION__
#elif defined(__FUNCSIG__)
	#define HZ_FUNC_SIG __FUNCSIG__
#elif (defined(__INTEL_COMPILER) && (__INTEL_COMPILER >= 600)) || (defined(__IBMCPP__) && (__IBMCPP__ >= 500))
	#define HZ_FUNC_SIG __FUNCTION__
#elif defined(__BORLANDC__) && (__BORLANDC__ >= 0x550)
	#define HZ_FUNC_SIG __FUNC__
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901)
	#define HZ_FUNC_SIG __func__
#elif defined(__cplusplus) && (__cplusplus >= 201103)
	#define HZ_FUNC_SIG __func__
#else
	#define HZ_FUNC_SIG "HZ_FUNC_SIG unknown!"
#endif

#define PROFILE_BEGIN_SESSION(name, filepath) ::Instrumentor::Get().BeginSession(name, filepath)
#define PROFILE_END_SESSION() ::Instrumentor::Get().EndSession()
#if NDEBUG
#define TIMER_MACRO() timer
#define LINE_MACRO() __LINE__
#define PROFILE_SCOPE(name) ::InstrumentationTimer TIMER_MACRO()LINE_MACRO()(name);
#else
#define PROFILE_SCOPE(name) ::InstrumentationTimer timer__LINE__(name);
#endif
#define PROFILE_FUNCTION() PROFILE_SCOPE(HZ_FUNC_SIG)