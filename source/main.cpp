#define NO_MALLOC_OVERRIDE

#include <GarrysMod/Lua/Interface.h>
#include <GarrysMod/FactoryLoader.hpp>
#include <scanning/symbolfinder.hpp>
#include <detouring/hook.hpp>
#include <iostream>
#include <iclient.h>
#include "ivoicecodec.h"
#include "audio_effects.h"
#include "net.h"
#include "thirdparty.h"
#include "steam_voice.h"
#include "eightbit_state.h"
#include <GarrysMod/Symbol.hpp>
#include <tier0/dbg.h>     // MsgC, Msg, Warning, DevMsg, etc.
#include <cstdint>
#include "opus_framedecoder.h"
#include <fstream>

#define STEAM_PCKT_SZ sizeof(uint64_t) + sizeof(CRC32_t)
#ifdef SYSTEM_WINDOWS
	#include <windows.h>

	const std::vector<Symbol> BroadcastVoiceSyms = {
#if defined ARCHITECTURE_X86
		Symbol::FromSignature("\x55\x8B\xEC\xA1****\x83\xEC\x50\x83\x78\x30\x00\x0F\x84****\x53\x8D\x4D\xD8\xC6\x45\xB4\x01\xC7\x45*****"),
		Symbol::FromSignature("\x55\x8B\xEC\x8B\x0D****\x83\xEC\x58\x81\xF9****"),
		Symbol::FromSignature("\x55\x8B\xEC\xA1****\x83\xEC\x50"),
#elif defined ARCHITECTURE_X86_64
		Symbol::FromSignature("\x48\x89\x5C\x24*\x56\x57\x41\x56\x48\x81\xEC****\x8B\xF2\x4C\x8B\xF1"),
#endif
	};
#endif

#ifdef SYSTEM_LINUX
	#include <dlfcn.h>
	const std::vector<Symbol> BroadcastVoiceSyms = {
		Symbol::FromName("_Z21SV_BroadcastVoiceDataP7IClientiPcx"),
		Symbol::FromSignature("\x55\x48\x8D\x05****\x48\x89\xE5\x41\x57\x41\x56\x41\x89\xF6\x41\x55\x49\x89\xFD\x41\x54\x49\x89\xD4\x53\x48\x89\xCB\x48\x81\xEC****\x48\x8B\x3D****\x48\x39\xC7\x74\x25"),
	};
#endif

alignas(16) static char decompressedBuffer[20 * 1024];
alignas(16) static char recompressBuffer[20 * 1024];

Net* net_handl = nullptr;
EightbitState* g_eightbit = nullptr;

bool LoadAudioFile(const std::string& filepath, std::vector<int16_t>& outBuffer) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        Warning("[Eightbit] Failed to open vocoder reference file: %s\n", filepath.c_str());
        return false;
    }

    // Read file into buffer
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    // Assume 16-bit PCM audio
    size_t sampleCount = fileSize / sizeof(int16_t);
    outBuffer.resize(sampleCount);
    
    file.read(reinterpret_cast<char*>(outBuffer.data()), fileSize);
    file.close();

    Msg("[Eightbit] Loaded vocoder reference: %s (%zu samples)\n", filepath.c_str(), sampleCount);
    return true;
}

typedef void (*SV_BroadcastVoiceData)(IClient* cl, int nBytes, char* data, int64 xuid);
Detouring::Hook detour_BroadcastVoiceData;

void hook_BroadcastVoiceData(IClient* cl, uint nBytes, char* data, int64 xuid) {
	int uid = cl->GetPlayerSlot() + 1;

	if (uid < 0 || uid > 128) 
        return detour_BroadcastVoiceData.GetTrampoline<SV_BroadcastVoiceData>()(cl, nBytes, data, xuid);

#ifdef THIRDPARTY_LINK
	if(checkIfMuted(cl->GetPlayerSlot()+1)) {
		return detour_BroadcastVoiceData.GetTrampoline<SV_BroadcastVoiceData>()(cl, nBytes, data, xuid);
	}
#endif

	if (g_eightbit->broadcastPackets && nBytes > sizeof(uint64_t)) {
#if defined ARCHITECTURE_X86
        uint64_t id64 = *(uint64_t*)((char*)cl + 181);
#else
        uint64_t id64 = *(uint64_t*)((char*)cl + 189);
#endif
        *(uint64_t*)decompressedBuffer = id64;
        size_t toCopy = nBytes - sizeof(uint64_t);
        std::memcpy(decompressedBuffer + sizeof(uint64_t), data + sizeof(uint64_t), toCopy);
        net_handl->SendPacket(g_eightbit->ip.c_str(), g_eightbit->port, decompressedBuffer, nBytes);
    }

	auto& pState = g_eightbit->players[uid];

	if (pState.effect != AudioEffects::EFF_NONE && pState.codec != nullptr) {
        if(nBytes < STEAM_PCKT_SZ) {
            return detour_BroadcastVoiceData.GetTrampoline<SV_BroadcastVoiceData>()(cl, nBytes, data, xuid);
        }

        int bytesDecompressed = SteamVoice::DecompressIntoBuffer(pState.codec, data, nBytes, decompressedBuffer, sizeof(decompressedBuffer));
        int samples = bytesDecompressed / 2;

        if (bytesDecompressed <= 0) {
            return detour_BroadcastVoiceData.GetTrampoline<SV_BroadcastVoiceData>()(cl, nBytes, data, xuid);
        }

        int16_t* pcmData = (int16_t*)decompressedBuffer;
		switch (pState.effect) {
		case AudioEffects::EFF_BITCRUSH:
			AudioEffects::BitCrush((uint16_t*)pcmData, samples, g_eightbit->crushFactor, g_eightbit->gainFactor);
			break;
		case AudioEffects::EFF_DESAMPLE:
			AudioEffects::Desample((uint16_t*)pcmData, samples, g_eightbit->desampleRate);
			break;

		// ajouter également ici les nouveaux effets
		case AudioEffects::EFF_ROBOT:
			AudioEffects::Robotize(pcmData, samples, 60.0f);
			break;
		case AudioEffects::EFF_DEMON:
			AudioEffects::Demon(pcmData, samples);
			break;
		case AudioEffects::EFF_INTERCOM:
			AudioEffects::Intercom(pcmData, samples);
			break;
		case AudioEffects::EFF_VOCODER:
			if (!pState.vocoderReference.empty()) {
				Warning("Debuggy: Voice Samples: %d\n",samples);
				AudioEffects::Vocoder(pcmData, pState.vocoderReference.data(), samples, pState.vocoderEnv, pState.vocoderAttack, pState.vocoderRelease, pState.vocoderGain);
			} else {
				Warning("Vocoder effect enabled but no reference sample loaded for player %d\n", uid);
			}
			break;
		default:
			break;
		}

		//Recompress the stream
		uint64_t steamid = *(uint64_t*)data;
		int bytesWritten = SteamVoice::CompressIntoBuffer(steamid, pState.codec, decompressedBuffer, samples*2, recompressBuffer, sizeof(recompressBuffer), 24000);
		if (bytesWritten <= 0) {
			return detour_BroadcastVoiceData.GetTrampoline<SV_BroadcastVoiceData>()(cl, nBytes, data, xuid);
		}

		return detour_BroadcastVoiceData.GetTrampoline<SV_BroadcastVoiceData>()(cl, bytesWritten, recompressBuffer, xuid);
	}
	else {
		return detour_BroadcastVoiceData.GetTrampoline<SV_BroadcastVoiceData>()(cl, nBytes, data, xuid);
	}
}

LUA_FUNCTION_STATIC(eightbit_crush) {
	g_eightbit->crushFactor = (int)LUA->GetNumber(1);
	return 0;
}

LUA_FUNCTION_STATIC(eightbit_setvocoderfilter) {
	int id = (int)LUA->GetNumber(1);
    std::string filepath = std::string(LUA->GetString());

	if (id < 0 || id > 128) return 0;

    auto& pState = g_eightbit->players[id];
    
    if (filepath.empty()) {
        pState.vocoderReference.clear();
        pState.vocoderPos = 0;
        Msg("[Eightbit] Cleared vocoder reference for player %d\n", id);
        return 0;
    }
    
    if (LoadAudioFile(filepath, pState.vocoderReference)) {
        pState.vocoderPos = 0;
        Msg("[Eightbit] Set vocoder reference for player %d\n", id);
    }

	return 0;
}

LUA_FUNCTION_STATIC(eightbit_gain) {
	g_eightbit->gainFactor = (float)LUA->GetNumber(1);
	return 0;
}

LUA_FUNCTION_STATIC(eightbit_setbroadcastip) {
	g_eightbit->ip = std::string(LUA->GetString());
	return 0;
}

LUA_FUNCTION_STATIC(eightbit_setbroadcastport) {
	g_eightbit->port = (uint16_t)LUA->GetNumber(1);
	return 0;
}

LUA_FUNCTION_STATIC(eightbit_broadcast) {
	g_eightbit->broadcastPackets = LUA->GetBool(1);
	return 0;
}

LUA_FUNCTION_STATIC(eightbit_getcrush) {
	LUA->PushNumber(g_eightbit->crushFactor);
	return 1;
}

LUA_FUNCTION_STATIC(eightbit_setdesamplerate) {
	g_eightbit->desampleRate = (int)LUA->GetNumber(1);
	return 0;
}

LUA_FUNCTION_STATIC(eightbit_setvocoderpos) {
    int id = (int)LUA->GetNumber(1);
    int pos = (size_t)LUA->GetNumber(2);

    if (id < 0 || id > 128) return 0;

    auto& pState = g_eightbit->players[id];

    pState.vocoderPos = pos;
    return 0;
}

LUA_FUNCTION_STATIC(eightbit_enableEffect) {
    int id = (int)LUA->GetNumber(1);
    int eff = (int)LUA->GetNumber(2);

    if (id < 0 || id > 128) return 0;

    auto& pState = g_eightbit->players[id];

    if (eff == AudioEffects::EFF_NONE) {
        if (pState.codec) {
            pState.codec->Release();
            pState.codec = nullptr;
        }
        pState.effect = AudioEffects::EFF_NONE;
    } else {
        if (!pState.codec) {
            pState.codec = new SteamOpus::Opus_FrameDecoder();
            pState.codec->Init(5, 24000);
        }
        pState.effect = eff;
    }
    return 0;
}

LUA_FUNCTION_STATIC(eightbit_clearPlayer) {
    int id = (int)LUA->GetNumber(1);
    if (id >= 0 && id <= 128) {
        if (g_eightbit->players[id].codec) {
            g_eightbit->players[id].codec->Release();
            g_eightbit->players[id].codec = nullptr;
        }
        g_eightbit->players[id].effect = 0;
    }
    return 0;
}

LUA_FUNCTION_STATIC(eightbit_setvocoderattack) {
	int id = (int)LUA->GetNumber(1);
	float attack = (float)LUA->GetNumber(2);
	
	if (id < 0 || id > 128) return 0;
	if (attack < 0.0f) attack = 0.0f;
	
	g_eightbit->players[id].vocoderAttack = attack;
	return 0;
}

LUA_FUNCTION_STATIC(eightbit_setvocoderrelease) {
	int id = (int)LUA->GetNumber(1);
	float release = (float)LUA->GetNumber(2);
	
	if (id < 0 || id > 128) return 0;
	if (release < 0.0f) release = 0.0f;
	
	g_eightbit->players[id].vocoderRelease = release;
	return 0;
}

LUA_FUNCTION_STATIC(eightbit_setvocordergain) {
	int id = (int)LUA->GetNumber(1);
	float gain = (float)LUA->GetNumber(2);
	
	if (id < 0 || id > 128) return 0;
	if (gain < 0.0f) gain = 0.0f;
	
	g_eightbit->players[id].vocoderGain = gain;
	return 0;
}


GMOD_MODULE_OPEN()
{
	Msg("[Eightbit] init...\n");

	g_eightbit = new EightbitState();

	for(int i=0; i<=128; i++) {
        g_eightbit->players[i].codec = nullptr;
        g_eightbit->players[i].effect = 0;
    }

	SourceSDK::ModuleLoader engine_loader("engine");
	SymbolFinder symfinder;

void* sv_bcast = nullptr;

    for (const auto& sym : BroadcastVoiceSyms) {
        sv_bcast = symfinder.Resolve(engine_loader.GetModule(), sym.name.c_str(), sym.length);
        if (sv_bcast) break;
    }

    if (!sv_bcast) LUA->ThrowError("Could not locate SV_BroadcastVoice symbol!");

	detour_BroadcastVoiceData.Create(Detouring::Hook::Target(sv_bcast), reinterpret_cast<void*>(&hook_BroadcastVoiceData));
	detour_BroadcastVoiceData.Enable();

	LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);

	LUA->PushString("eightbit");
	LUA->CreateTable();
		LUA->PushString("SetCrushFactor");
		LUA->PushCFunction(eightbit_crush);
		LUA->SetTable(-3);

		LUA->PushString("SetVocoderFilter");
		LUA->PushCFunction(eightbit_setvocoderfilter);
		LUA->SetTable(-3);

		LUA->PushString("SetVocoderAttack");
		LUA->PushCFunction(eightbit_setvocoderattack);
		LUA->SetTable(-3);

		LUA->PushString("SetVocoderPos");
		LUA->PushCFunction(eightbit_setvocoderpos);
		LUA->SetTable(-3);

		LUA->PushString("SetVocoderRelease");
		LUA->PushCFunction(eightbit_setvocoderrelease);
		LUA->SetTable(-3);

		LUA->PushString("SetVocoderGain");
		LUA->PushCFunction(eightbit_setvocordergain);
		LUA->SetTable(-3);

		LUA->PushString("GetCrushFactor");
		LUA->PushCFunction(eightbit_getcrush);
		LUA->SetTable(-3);

		LUA->PushString("EnableEffect");
		LUA->PushCFunction(eightbit_enableEffect);
		LUA->SetTable(-3);

		LUA->PushString("EnableBroadcast");
		LUA->PushCFunction(eightbit_broadcast);
		LUA->SetTable(-3);

		LUA->PushString("SetGainFactor");
		LUA->PushCFunction(eightbit_gain);
		LUA->SetTable(-3);

		LUA->PushString("SetDesampleRate");
		LUA->PushCFunction(eightbit_setdesamplerate);
		LUA->SetTable(-3);

		LUA->PushString("SetBroadcastIP");
		LUA->PushCFunction(eightbit_setbroadcastip);
		LUA->SetTable(-3);

		LUA->PushString("SetBroadcastPort");
		LUA->PushCFunction(eightbit_setbroadcastport);
		LUA->SetTable(-3);

		LUA->PushString("ClearPlayer"); 
		LUA->PushCFunction(eightbit_clearPlayer); 
		LUA->SetTable(-3);

		// AUDIO EFFECT ENUMS

		LUA->PushString("EFF_NONE");
		LUA->PushNumber(AudioEffects::EFF_NONE);
		LUA->SetTable(-3);

		LUA->PushString("EFF_DESAMPLE");
		LUA->PushNumber(AudioEffects::EFF_DESAMPLE);
		LUA->SetTable(-3);

		LUA->PushString("EFF_BITCRUSH");
		LUA->PushNumber(AudioEffects::EFF_BITCRUSH);
		LUA->SetTable(-3);

		LUA->PushString("EFF_ROBOT");
		LUA->PushNumber(AudioEffects::EFF_ROBOT);
		LUA->SetTable(-3);

		LUA->PushString("EFF_DEMON");
		LUA->PushNumber(AudioEffects::EFF_DEMON);
		LUA->SetTable(-3);

		LUA->PushString("EFF_INTERCOM");
		LUA->PushNumber(AudioEffects::EFF_INTERCOM);
		LUA->SetTable(-3);

	LUA->SetTable(-3);
	LUA->Pop();

	net_handl = new Net();

#ifdef THIRDPARTY_LINK
	linkMutedFunc();
#endif

	Msg("[Eightbit] Module binaire chargé avec succès !\n");
	Msg("[Eightbit] Bienvenue sur le module '<Eightbit>'\n");

    return 0;
}

GMOD_MODULE_CLOSE()
{
	detour_BroadcastVoiceData.Disable();
	detour_BroadcastVoiceData.Destroy();

	for (int i = 0; i <= 128; i++) {
        if (g_eightbit->players[i].codec) {
            g_eightbit->players[i].codec->Release();
        }
    }

	delete net_handl;
	delete g_eightbit;

	return 0;
}
