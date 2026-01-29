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
    void Vocoder(int16_t* carrier,int16_t* modulator,int samples,float& env,float attack,float release,float gain) {
        // persistent DC blocker for modulator
        static float modPrev = 0.0f;
        static float dc = 0.0f;

        for (int i = 0; i < samples; i++) {
            // normalize modulator
            float m = modulator[i] * (1.0f / 32768.0f);

            // simple DC blocker (high-pass)
            dc = m - modPrev + 0.995f * dc;
            modPrev = m;

            // envelope detection (rectified, NOT squared)
            float mag = fabsf(dc);

            // proper attack / release smoothing
            if (mag > env)
                env += (mag - env) * attack;
            else
                env += (mag - env) * release;

            // carrier
            float c = carrier[i] * (1.0f / 32768.0f);

            // floor avoids total silence + zipper noise
            float e = fmaxf(env, 0.015f);

            float out = c * e * gain;

            // soft clip instead of hard clamp (sounds smoother)
            out = out / (1.0f + fabsf(out));

            carrier[i] = (int16_t)(out * 32767.0f);
        }
    }
}