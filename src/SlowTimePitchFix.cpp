// SlowTimePitchFix - SKSE plugin for Skyrim Special Edition / Anniversary Edition (1.6.x AE and 1.7.x)
//
// 1) Gamepad pitch fix (always useful, on by default)
//    While time is slowed (Slow Time shout, time-stop rings, etc.) the gamepad right stick turns
//    left/right at normal speed but looks up/down extremely slowly.
//    Cause: the gamepad look routine (Address Library ID 42354) time-normalizes ONLY the pitch value:
//        y *= min(1 / fLookTimeNormalizingFloorFramerate, frameDelta) * fLookTimeNormalizingTargetFramerate
//    and frameDelta is the game-time frame delta (ID 410199), which is scaled by the time multiplier.
//    Fix: make that one instruction (minss xmm0, [rip+disp32]) read the real-time frame delta (ID 410200).
//    Outside of slowed time both values are identical, so normal play is unchanged.
//
// 2) Optional: player speed during Slow Time effects (off by default)
//    Slow Time effects slow the player to the game setting fVATSPlayerMagicTimeSlowdownMult
//    (Skyrim.esm: 0.6), independent of how much the world is slowed. The INI can override it.
//    The INI is re-read after every save load / new game, so changes apply without restarting.
//
// Safety: nothing is changed unless the IDs resolve through Address Library (format 2 for 1.6.x,
// format 5 for 1.7.x) and the expected code/setting is found at runtime. Results are written to
// Documents\My Games\Skyrim Special Edition\SKSE\SlowTimePitchFix.log

#include <windows.h>
#include <shlobj.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

#include "AddressLibrary.h"

using UInt32 = std::uint32_t;
using PluginHandle = UInt32;

struct SKSEInterface
{
	UInt32 skseVersion;
	UInt32 runtimeVersion;
	UInt32 editorVersion;
	UInt32 isEditor;
	void* (*QueryInterface)(UInt32 id);
	PluginHandle (*GetPluginHandle)();
};

struct SKSEMessagingInterface
{
	struct Message
	{
		const char* sender;
		UInt32 type;
		UInt32 dataLen;
		void* data;
	};
	using EventCallback = void (*)(Message* msg);

	UInt32 interfaceVersion;
	bool (*RegisterListener)(PluginHandle listener, const char* sender, EventCallback handler);
};

struct SKSEPluginVersionData
{
	UInt32 dataVersion;
	UInt32 pluginVersion;
	char name[256];
	char author[256];
	char supportEmail[252];
	UInt32 versionIndependenceEx;
	UInt32 versionIndependence;
	UInt32 compatibleVersions[16];
	UInt32 seVersionRequired;
};
static_assert(sizeof(SKSEPluginVersionData) == 848, "SKSEPluginVersionData layout");

extern "C" __declspec(dllexport) SKSEPluginVersionData SKSEPlugin_Version = {
	1,                    // dataVersion (kVersion)
	0x01000000,           // pluginVersion 1.0.0
	"SlowTimePitchFix",   // name
	"",                   // author
	"",                   // supportEmail
	1,                    // versionIndependenceEx: kVersionIndependentEx_NoStructUse
	1,                    // versionIndependence: kVersionIndependent_AddressLibraryPostAE
	{ 0 },
	0
};

namespace
{
	constexpr UInt32 kInterface_Messaging = 5;
	constexpr UInt32 kMessage_PostLoadGame = 3;
	constexpr UInt32 kMessage_NewGame = 7;
	constexpr UInt32 kMessage_DataLoaded = 8;

	constexpr UInt32 kID_GamepadLook = 42354;          // gamepad look processing
	constexpr UInt32 kID_FrameDeltaGame = 410199;      // seconds since last frame (game time)
	constexpr UInt32 kID_FrameDeltaReal = 410200;      // seconds since last frame (real time)
	constexpr UInt32 kID_PlayerSlowTimeSetting = 373402;  // Setting fVATSPlayerMagicTimeSlowdownMult
	constexpr const char* kPlayerSlowTimeSettingName = "fVATSPlayerMagicTimeSlowdownMult";
	constexpr std::size_t kScanLength = 0x600;

	FILE* g_log = nullptr;
	std::wstring g_iniPath;
	std::wstring g_pluginDir;
	float* g_playerSlowTime = nullptr;
	float g_playerSlowTimeOriginal = 0.0f;
	bool g_haveOriginal = false;

	void OpenLog()
	{
		PWSTR docs = nullptr;
		if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs))) {
			std::wstring path = docs;
			path += L"\\My Games\\Skyrim Special Edition\\SKSE\\SlowTimePitchFix.log";
			_wfopen_s(&g_log, path.c_str(), L"w");
		}
		if (docs) {
			CoTaskMemFree(docs);
		}
	}

	void Log(const char* fmt, ...)
	{
		if (!g_log) {
			return;
		}
		va_list ap;
		va_start(ap, fmt);
		std::vfprintf(g_log, fmt, ap);
		va_end(ap);
		std::fputc('\n', g_log);
		std::fflush(g_log);
	}

	void InitPaths()
	{
		HMODULE self = nullptr;
		GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&InitPaths), &self);
		wchar_t buf[MAX_PATH * 2] = {};
		GetModuleFileNameW(self, buf, static_cast<DWORD>(std::size(buf)));
		std::wstring dll = buf;
		const auto slash = dll.find_last_of(L"\\/");
		g_pluginDir = slash == std::wstring::npos ? L"" : dll.substr(0, slash + 1);
		const auto dot = dll.find_last_of(L'.');
		g_iniPath = (dot == std::wstring::npos ? dll : dll.substr(0, dot)) + L".ini";
	}

	// Returns `fallback` if the key is missing/empty or not a number.
	float IniFloat(const wchar_t* section, const wchar_t* key, float fallback)
	{
		wchar_t buf[64] = {};
		GetPrivateProfileStringW(section, key, L"", buf, static_cast<DWORD>(std::size(buf)), g_iniPath.c_str());
		wchar_t* end = nullptr;
		const float v = std::wcstof(buf, &end);
		return (end == buf) ? fallback : v;
	}

	bool IniBool(const wchar_t* section, const wchar_t* key, bool fallback)
	{
		return IniFloat(section, key, fallback ? 1.0f : 0.0f) != 0.0f;
	}

	void PatchGamepadPitch(std::uint64_t fnOff, std::uint64_t gameOff, std::uint64_t realOff, std::uintptr_t base)
	{
		const std::uintptr_t fn = base + fnOff;
		const std::uintptr_t gameDelta = base + gameOff;
		const std::uintptr_t realDelta = base + realOff;

		std::uint8_t* match = nullptr;
		int matches = 0;
		for (std::size_t i = 0; i + 8 <= kScanLength; ++i) {
			auto* p = reinterpret_cast<std::uint8_t*>(fn + i);
			if (p[0] == 0xF3 && p[1] == 0x0F && p[2] == 0x5D && p[3] == 0x05) {  // minss xmm0, [rip+disp32]
				std::int32_t disp;
				std::memcpy(&disp, p + 4, 4);
				if (reinterpret_cast<std::uintptr_t>(p + 8) + disp == gameDelta) {
					match = p;
					++matches;
				}
			}
		}
		if (matches != 1) {
			Log("pitch fix SKIP: expected 1 pitch normalization instruction, found %d", matches);
			return;
		}

		const auto newDisp = static_cast<std::int32_t>(
			static_cast<std::int64_t>(realDelta) - static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(match + 8)));
		DWORD oldProtect = 0;
		if (!VirtualProtect(match + 4, 4, PAGE_EXECUTE_READWRITE, &oldProtect)) {
			Log("pitch fix SKIP: VirtualProtect failed (%lu)", GetLastError());
			return;
		}
		std::memcpy(match + 4, &newDisp, 4);
		VirtualProtect(match + 4, 4, oldProtect, &oldProtect);
		FlushInstructionCache(GetCurrentProcess(), match, 8);
		Log("pitch fix OK: patched SkyrimSE.exe+%llX (gamepad pitch uses real-time frame delta +%llX instead of game-time +%llX)",
			static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(match) - base),
			static_cast<unsigned long long>(realOff), static_cast<unsigned long long>(gameOff));
	}

	void LocatePlayerSlowTimeSetting(std::uint64_t off, std::uintptr_t base)
	{
		// Setting layout: vtable (8), value (8), name (const char*)
		const auto setting = base + off;
		const char* name = *reinterpret_cast<const char* const*>(setting + 0x10);
		const auto lo = base;
		const auto hi = base + 0x8000000;
		if (reinterpret_cast<std::uintptr_t>(name) < lo || reinterpret_cast<std::uintptr_t>(name) >= hi ||
			std::strcmp(name, kPlayerSlowTimeSettingName) != 0) {
			Log("player speed SKIP: setting at +%llX is not %s", static_cast<unsigned long long>(off), kPlayerSlowTimeSettingName);
			return;
		}
		g_playerSlowTime = reinterpret_cast<float*>(setting + 8);
	}

	void ApplyPlayerSpeed(const char* when)
	{
		if (!g_playerSlowTime) {
			return;
		}
		if (!g_haveOriginal) {
			g_playerSlowTimeOriginal = *g_playerSlowTime;  // value after all plugins (e.g. Skyrim.esm) loaded
			g_haveOriginal = true;
		}
		float v = IniFloat(L"SlowTime", L"fPlayerSpeedMult", -1.0f);
		if (v < 0.0f) {
			*g_playerSlowTime = g_playerSlowTimeOriginal;
			Log("[%s] player speed during Slow Time: game value %.3f (not overridden)", when, g_playerSlowTimeOriginal);
			return;
		}
		if (v < 0.05f) v = 0.05f;
		if (v > 1.0f) v = 1.0f;
		*g_playerSlowTime = v;
		Log("[%s] player speed during Slow Time: %.3f (game value %.3f)", when, v, g_playerSlowTimeOriginal);
	}

	void OnMessage(SKSEMessagingInterface::Message* msg)
	{
		if (!msg) {
			return;
		}
		switch (msg->type) {
		case kMessage_DataLoaded:
			ApplyPlayerSpeed("data loaded");
			break;
		case kMessage_PostLoadGame:
			ApplyPlayerSpeed("save loaded");
			break;
		case kMessage_NewGame:
			ApplyPlayerSpeed("new game");
			break;
		default:
			break;
		}
	}
}

extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const SKSEInterface* skse)
{
	OpenLog();
	Log("SlowTimePitchFix 1.0.0");
	if (!skse || skse->isEditor) {
		Log("SKIP: editor or no interface");
		return false;
	}
	InitPaths();
	Log("ini: %ls", g_iniPath.c_str());

	const UInt32 v = skse->runtimeVersion;
	wchar_t libName[96];
	std::swprintf(libName, std::size(libName), L"versionlib-%u-%u-%u-%u.bin", v >> 24, (v >> 16) & 0xFF, (v >> 4) & 0xFFF, v & 0xF);
	Log("runtime %u.%u.%u.%u, address library: %ls", v >> 24, (v >> 16) & 0xFF, (v >> 4) & 0xFFF, v & 0xF, libName);

	const std::uint32_t ids[] = { kID_GamepadLook, kID_FrameDeltaGame, kID_FrameDeltaReal, kID_PlayerSlowTimeSetting };
	std::uint64_t off[4] = {};
	AddressLibrary al;
	if (!al.Resolve(g_pluginDir + libName, ids, 4, off)) {
		Log("SKIP: %s (format %u). Install Address Library for SKSE Plugins (All in one).", al.error, al.format);
		return true;
	}
	const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));

	if (IniBool(L"General", L"bFixGamepadPitch", true)) {
		PatchGamepadPitch(off[0], off[1], off[2], base);
	} else {
		Log("pitch fix disabled in ini");
	}

	LocatePlayerSlowTimeSetting(off[3], base);
	if (g_playerSlowTime) {
		auto* messaging = static_cast<SKSEMessagingInterface*>(skse->QueryInterface(kInterface_Messaging));
		if (messaging && messaging->RegisterListener(skse->GetPluginHandle(), "SKSE", OnMessage)) {
			Log("player speed option ready (applied after data load and on every save load)");
		} else {
			Log("player speed SKIP: messaging interface unavailable");
		}
	}
	return true;
}
