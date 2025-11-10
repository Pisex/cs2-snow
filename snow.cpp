#include <stdio.h>
#include "snow.h"
#include "metamod_oslink.h"
#include "schemasystem/schemasystem.h"

snow g_snow;
PLUGIN_EXPOSE(snow, g_snow);
IVEngineServer2* engine = nullptr;
CGameEntitySystem* g_pGameEntitySystem = nullptr;
CEntitySystem* g_pEntitySystem = nullptr;
CGlobalVars *gpGlobals = nullptr;

IUtilsApi* g_pUtils;
IPlayersApi* g_pPlayers;

bool g_bSnow[64];
CHandle<CParticleSystem> g_pSnow[64];
const char* g_szSnow;

KeyValues* g_hKVData;

std::map<std::string, std::string> g_vecPhrases;

SH_DECL_HOOK8_void(ISource2GameEntities, CheckTransmit, SH_NOATTRIB, 0, CCheckTransmitInfo **, int, CBitVec<16384> &, CBitVec<16384> &, const Entity2Networkable_t **, const uint16 *, int, bool);

void CreateSnow(int iSlot);

CGameEntitySystem* GameEntitySystem()
{
	return g_pUtils->GetCGameEntitySystem();
}

void StartupServer()
{
	g_pGameEntitySystem = GameEntitySystem();
	g_pEntitySystem = g_pUtils->GetCEntitySystem();
	gpGlobals = g_pUtils->GetCGlobalVars();
}

bool snow::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);

	GET_V_IFACE_ANY(GetServerFactory, g_pSource2GameEntities, ISource2GameEntities, SOURCE2GAMEENTITIES_INTERFACE_VERSION);
	SH_ADD_HOOK(ISource2GameEntities, CheckTransmit, g_pSource2GameEntities, SH_MEMBER(this, &snow::OnCheckTransmit), true);

	g_SMAPI->AddListener( this, this );

	return true;
}

void snow::OnCheckTransmit(CCheckTransmitInfo **pInfoInfoList, int nInfoCount, CBitVec<16384> &unionTransmitEdicts, CBitVec<16384> &, const Entity2Networkable_t **pNetworkables, const uint16 *pEntityIndicies, int nEntityIndices, bool bEnablePVSBits)
{
	if (!g_pEntitySystem)
		return;

	for (int i = 0; i < nInfoCount; i++)
	{
		auto &pInfo = pInfoInfoList[i];
		int iPlayerSlot = pInfo->m_nPlayerSlot;
		
		CCSPlayerController* pSelfController = CCSPlayerController::FromSlot(iPlayerSlot);
		if (!pSelfController || !pSelfController->IsConnected())
			continue;
		
		for (int j = 0; j < 64; j++)
		{
			CCSPlayerController* pController = CCSPlayerController::FromSlot(j);

			if (!pController || j == iPlayerSlot)
				continue;

			bool bObserver = pSelfController->GetPawnState() == STATE_OBSERVER_MODE && pSelfController->GetObserverTarget() == pController->GetPawn();
			if(g_pSnow[j] && !bObserver)
				pInfo->m_pTransmitEntity->Clear(g_pSnow[j]->entindex());
		}
	}
}

bool snow::Unload(char *error, size_t maxlen)
{
	ConVar_Unregister();
	SH_REMOVE_HOOK(ISource2GameEntities, CheckTransmit, g_pSource2GameEntities, SH_MEMBER(this, &snow::OnCheckTransmit), true);
	
	return true;
}

void KillSnow(int iSlot)
{
	if(g_pSnow[iSlot])
	{
		g_pUtils->RemoveEntity(g_pSnow[iSlot]);
		g_pSnow[iSlot] = nullptr;
	}
}

void CreateSnow(int iSlot)
{
	CCSPlayerController* pPlayer = CCSPlayerController::FromSlot(iSlot);
	if(!pPlayer) return;
	CCSPlayerPawn* pPlayerPawn = pPlayer->GetPlayerPawn();
	if(!pPlayerPawn) return;
	QAngle ang = pPlayerPawn->m_angEyeAngles();
	Vector pos = pPlayerPawn->GetAbsOrigin();
	pos.z += 200;
	CParticleSystem* pEnt = (CParticleSystem*)g_pUtils->CreateEntityByName("info_particle_system", -1);
	CHandle<CParticleSystem> hEnt = pEnt->GetHandle();
	g_pSnow[iSlot] = hEnt;
	pEnt->m_bStartActive(true);
	pEnt->m_iszEffectName(g_szSnow);
	g_pUtils->TeleportEntity(pEnt, &pos, &ang, nullptr);
	g_pUtils->DispatchSpawn(pEnt, nullptr);
	g_pUtils->AcceptEntityInput(pEnt, "SetParent", "!activator", pPlayerPawn, pEnt);
}

bool GetClientSnow(int iSlot)
{
	CCSPlayerController* pController = CCSPlayerController::FromSlot(iSlot);
	if (!pController)
		return true;
	uint32 m_steamID = pController->m_steamID();
	if(m_steamID == 0)
		return true;
	return g_hKVData->GetBool(std::to_string(m_steamID).c_str(), true);
}

void SaveClientSnow(int iSlot)
{
	CCSPlayerController* pController = CCSPlayerController::FromSlot(iSlot);
	if (!pController)
		return;
	uint32 m_steamID = pController->m_steamID();
	if(m_steamID == 0)
		return;
	g_hKVData->SetBool(std::to_string(m_steamID).c_str(), g_bSnow[iSlot]);
	g_hKVData->SaveToFile(g_pFullFileSystem, "addons/data/snow_data.ini");
}

bool OnSnowCommand(int iSlot, const char* szContent)
{
	if(g_bSnow[iSlot])
	{
		g_bSnow[iSlot] = false;
		SaveClientSnow(iSlot);
		KillSnow(iSlot);
		g_pUtils->PrintToChat(iSlot, g_vecPhrases["Snow_Disable"].c_str());
	}
	else
	{
		g_bSnow[iSlot] = true;
		SaveClientSnow(iSlot);
		CreateSnow(iSlot);
		g_pUtils->PrintToChat(iSlot, g_vecPhrases["Snow_Enable"].c_str());
	}
	return false;
}

bool LoadData()
{
	g_hKVData = new KeyValues("Data");

	const char *pszPath = "addons/data/snow_data.ini";

	if (!g_hKVData->LoadFromFile(g_pFullFileSystem, pszPath))
	{
		g_pUtils->ErrorLog("[%s] Failed to load %s", g_PLAPI->GetLogTag(), pszPath);
		return false;
	}

	return true;
}

void LoadTranslations()
{
	KeyValues::AutoDelete g_kvPhrases("Phrases");
	const char *pszPath = "addons/translations/snow.phrases.txt";
	if (!g_kvPhrases->LoadFromFile(g_pFullFileSystem, pszPath))
	{
		Warning("Failed to load %s\n", pszPath);
		return;
	}

	std::string szLanguage = std::string(g_pUtils->GetLanguage());
	const char* g_pszLanguage = szLanguage.c_str();
	for (KeyValues *pKey = g_kvPhrases->GetFirstTrueSubKey(); pKey; pKey = pKey->GetNextTrueSubKey())
		g_vecPhrases[std::string(pKey->GetName())] = std::string(pKey->GetString(g_pszLanguage));
}

void OnClientAuthorized(int iSlot, uint64 iSteamID64)
{
	g_bSnow[iSlot] = GetClientSnow(iSlot);
}

void snow::AllPluginsLoaded()
{
	char error[64];
	int ret;
	g_pUtils = (IUtilsApi *)g_SMAPI->MetaFactory(Utils_INTERFACE, &ret, NULL);
	if (ret == META_IFACE_FAILED)
	{
		g_SMAPI->Format(error, sizeof(error), "Missing Utils system plugin");
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}
	
	g_pPlayers = (IPlayersApi *)g_SMAPI->MetaFactory(PLAYERS_INTERFACE, &ret, NULL);
	if (ret == META_IFACE_FAILED)
	{
		g_SMAPI->Format(error, sizeof(error), "Missing Players system plugin");
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}
	
	g_pPlayers->HookOnClientAuthorized(g_PLID, OnClientAuthorized);
	g_pUtils->StartupServer(g_PLID, StartupServer);
	g_pUtils->HookEvent(g_PLID, "player_spawn", [](const char* szName, IGameEvent* pEvent, bool bDontBroadcast)
	{
		int iSlot = pEvent->GetInt("userid");
		if(g_bSnow[iSlot])
		{
			CreateSnow(iSlot);
		}
	});

	KeyValues* hKV = new KeyValues("Config");
	const char *pszPath = "addons/configs/snow.ini";
	if (!hKV->LoadFromFile(g_pFullFileSystem, pszPath))
	{
		g_pUtils->ErrorLog("[%s] Failed to load %s", GetLogTag(), pszPath);
		return;
	}
	g_szSnow = hKV->GetString("snow", "particles/snow.vpcf");
	if(!LoadData())
	{
		g_pUtils->ErrorLog("[%s] Failed to load data", GetLogTag());
	}
	LoadTranslations();
	g_pUtils->RegCommand(g_PLID, {"mm_snow"}, {"!snow"}, OnSnowCommand);
}

///////////////////////////////////////
const char* snow::GetLicense()
{
	return "GPL";
}

const char* snow::GetVersion()
{
	return "1.0";
}

const char* snow::GetDate()
{
	return __DATE__;
}

const char *snow::GetLogTag()
{
	return "snow";
}

const char* snow::GetAuthor()
{
	return "Pisex";
}

const char* snow::GetDescription()
{
	return "Snow";
}

const char* snow::GetName()
{
	return "Snow";
}

const char* snow::GetURL()
{
	return "https://discord.gg/g798xERK5Y";
}
