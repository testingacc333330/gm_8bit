#pragma once
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cmath>  
#include <vector>
#include <algorithm>

namespace AudioEffects {
	enum {
		EFF_NONE,
		EFF_BITCRUSH,
		EFF_DESAMPLE,
		EFF_ROBOT,
        EFF_DEMON,
		EFF_INTERCOM,
        EFF_VOCODER
	};

	void BitCrush(uint16_t* sampleBuffer, int samples, float quant, float gainFactor) {
		for (int i = 0; i < samples; i++) {
			//Signed shorts range from -32768 to 32767
			//Let's quantize that a bit
			float f = (float)sampleBuffer[i];
			f /= quant;
			sampleBuffer[i] = (uint16_t)f;
			sampleBuffer[i] *= quant;
			sampleBuffer[i] *= gainFactor;
		}
	}

	static uint16_t tempBuf[10 * 1024];
	void Desample(uint16_t* inBuffer, int& samples, int desampleRate = 2) {
		assert(samples / desampleRate + 1 <= sizeof(tempBuf));
		int outIdx = 0;
		for (int i = 0; i < samples; i++) {
			if (i % desampleRate == 0) continue;

			tempBuf[outIdx] = inBuffer[i];
			outIdx++;
		}
		std::memcpy(inBuffer, tempBuf, outIdx * 2);
		samples = outIdx;
	}

	//

    void ApplyEcho(int16_t* buffer, int samples, std::vector<int16_t>& echoBuf, size_t& echoPos, float decay = 0.5f, int delaySamples = 4800) {
        if (echoBuf.empty()) echoBuf.resize(24000, 0); // 1 sec buffer init

        for (int i = 0; i < samples; i++) {
            int16_t current = buffer[i];
            
            // Lire le sample passé (retardé)
            size_t readPos = (echoPos + echoBuf.size() - delaySamples) % echoBuf.size();
            int16_t delayed = echoBuf[readPos];

            // Écrire le sample courant + feedback dans le buffer
            // (Soft clipping simple pour éviter l'overflow immédiat)
            int mixed = (int)(current + delayed * decay);
            if (mixed > 32767) mixed = 32767;
            if (mixed < -32768) mixed = -32768;

            echoBuf[echoPos] = (int16_t)mixed;
            
            // Sortie audio
            buffer[i] = (int16_t)mixed;

            echoPos = (echoPos + 1) % echoBuf.size();
        }
    }

    void Robotize(int16_t* buffer, int samples, float freqHz, int sampleRate = 24000) {
        static float phase = 0.0f;
        float phaseIncrement = (2.0f * 3.14159f * freqHz) / (float)sampleRate;

        for (int i = 0; i < samples; i++) {
            float modulator = sinf(phase);
            buffer[i] = (int16_t)(buffer[i] * modulator);
            
            phase += phaseIncrement;
            if (phase > 2.0f * 3.14159f) phase -= 2.0f * 3.14159f;
        }
    }

    void Demon(int16_t* buffer, int samples) {
        Robotize(buffer, samples, 30.0f); // Modulation 30Hz (voix tremblante grave)
        // + Saturation légère
        for(int i=0; i<samples; i++) {
            buffer[i] = buffer[i] * 1.5f; // Boost gain
            if(buffer[i] > 20000) buffer[i] = 20000; // Hard clip
            if(buffer[i] < -20000) buffer[i] = -20000;
        }
    }

	// Effet Intercom / Radio
    void Intercom(int16_t* buffer, int samples) {
        static float lastSample = 0;
        for (int i = 0; i < samples; i++) {
            float s = (float)buffer[i];

            // 1. Simulation Passe-Haut simple (enlève les basses étouffées)
            float highPassed = s - lastSample;
            lastSample = s;

            // 2. Saturation (Overdrive de radio)
            if (highPassed > 15000) highPassed = 15000;
            if (highPassed < -15000) highPassed = -15000;

            int16_t noise = (rand() % 200) - 100; 
        
            // Mélange
            int mixed = highPassed + noise;
            
            // Clamp (sécurité saturation)
            if (mixed > 32767) mixed = 32767;
            else if (mixed < -32768) mixed = -32768;
            
            buffer[i] = (int16_t)mixed;
        }
    }

    // unusual parameters, but needed for customizability
    void Vocoder(int16_t* carrier, int16_t* modulator, int samples, size_t& modPos, size_t modulatorSize, float& env) {
        const float attack  = 0.28f;
        const float release = 0.992f;
        const float gain = 1.6f;

        if (modulatorSize == 0) return;

        for (int i = 0; i < samples; i++) {
            // --- Modulator envelope (audio file) ---
            float m = (float)modulator[modPos];
            float absM = fabsf(m);

            if (absM > env)
                env = attack * absM + (1.0f - attack) * env;
            else
                env = release * env + (1.0f - release) * absM;

            // --- Apply envelope to carrier (voice) ---
            float c = (float)carrier[i];
            float out = c * (env / 16000.0f) * gain;

            // Clamp
            if (out > 32767) out = 32767;
            if (out < -32768) out = -32768;

            carrier[i] = (int16_t)out;
            
            // Advance position with wrapping
            modPos = (modPos + 1) % modulatorSize;
        }
    }

}