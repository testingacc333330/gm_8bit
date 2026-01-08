#pragma once
#include <string>
#include <unordered_map>

struct PlayerState {
	IVoiceCodec* codec = nullptr;
	int effect = 0;
};

struct EightbitState {
	int crushFactor = 350;
	float gainFactor = 1.2;
	bool broadcastPackets = false;
	int desampleRate = 2;
	uint16_t port = 4000;
	std::string ip = "127.0.0.1";
	PlayerState afflictedPlayers[129]; // 0 à 128
};
