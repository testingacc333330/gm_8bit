#pragma once
#include <string>
#include <vector>

struct PlayerState {
	IVoiceCodec* codec = nullptr;
	int effect = 0;
    // Paramètres d'effets
    float param1 = 0.0f; 
    
    // Buffer pour l'écho
    std::vector<int16_t> echoBuffer;
    size_t echoPos = 0;
    
    // Vocoder reference sample
    std::vector<int16_t> vocoderReference;
    size_t vocoderPos = 0;
    float vocoderEnv = 1.0f;

    float vocoderAttack = 0.01f;
    float vocoderRelease = 0.0015f;
    float vocoderGain = 1.2f;
};

struct EightbitState {
	int crushFactor = 350;
	float gainFactor = 1.2;
	bool broadcastPackets = false;
	int desampleRate = 2;
	uint16_t port = 4000;
	std::string ip = "127.0.0.1";
	PlayerState players[129]; // 0 à 128
};
