# note from me

don't expect the new changes to work
if you ask yourself why so many release with super minor changes it's because my actions wont work locally

so have fun with these 1 billion releases i guess?

# gm_8bit

A module for manipulating voice data in Garry's Mod.

## What does it do?

**gm_8bit** is designed to be a starting point for any kind of voice stream manipulation you might want to do on a Garry's Mod server (or any source engine server, with a bit of adjustment).

It can decompress and recompress Steam voice packets (OPUS). It includes an `SV_BroadcastVoiceData` hook to allow server operators to intercept and manipulate this voice data. It makes several things possible, including:

* **Relaying server voice data** to external locations (e.g., Discord bots).
* **Performing voice recognition** and producing transcripts.
* **Recording voice data** in compressed or uncompressed form.
* **Applying transformation** to user voice streams in real-time (pitch correction, noise suppression, gain control, DSP effects).

## Installation

1. Grab the latest binary from the **Releases** or **Actions** page.
2. Place the module (`gmsv_eightbit_win64.dll` or `gmsv_eightbit_linux.dll`) into your server's `garrysmod/lua/bin/` folder.
3. Create a Lua script in `lua/autorun/server/` to require the module: `require("eightbit")`.

---

## Lua API Documentation

### General Effects

| Function | Description |
| --- | --- |
| `eightbit.EnableEffect(entIndex, effectEnum)` | Enables or disables an audio effect for a specific player entity index (0-128). Pass `eightbit.EFF_NONE` to disable. |
| `eightbit.ClearPlayer(entIndex)` | Completely resets the audio state/codecs for a specific player index. |

### Global Settings (Parameters)

| Function | Description |
| --- | --- |
| `eightbit.SetGainFactor(number)` | Sets the global volume multiplier (Gain) for active effects. |
| `eightbit.SetCrushFactor(number)` | Sets the intensity for the `EFF_BITCRUSH` effect (Lower is often harsher). |
| `eightbit.SetDesampleRate(number)` | Sets the sample reduction rate for `EFF_DESAMPLE` (Integer, e.g., 2, 3, 4). |

### Broadcasting (Voice Relay)

| Function | Description |
| --- | --- |
| `eightbit.EnableBroadcast(bool)` | Sets whether the module should relay voice packets to an external UDP server. |
| `eightbit.SetBroadcastIP(string)` | Sets the target IP address for the voice relay (default: "127.0.0.1"). |
| `eightbit.SetBroadcastPort(number)` | Sets the target UDP port for the voice relay (default: 4000). |

### Effect Enums

Use these constants with `eightbit.EnableEffect`:

* `eightbit.EFF_NONE` : No effect (Standard voice).
* `eightbit.EFF_BITCRUSH` : "Deep fries" the audio. Quantizes the signal heavily.
* `eightbit.EFF_DESAMPLE` : Reduces the sample rate, creating a lo-fi aliasing effect.
* `eightbit.EFF_ROBOT` : Applies a ring modulation (sine wave) to simulate a robotic voice.
* `eightbit.EFF_DEMON` : Lowers pitch perception and applies saturation for a monstrous voice.
* `eightbit.EFF_INTERCOM` : Simulates a radio/walkie-talkie (High-pass filter + Distortion).

---

## Example: Model-Based Voice Changer

Here is a complete example script. It automatically detects the player's model and applies a specific voice effect (Robot, Demon, or Radio).

Place this in `lua/autorun/server/sv_voice_manager.lua`.

```lua
require("eightbit")

-- Configuration: Map player models to Audio Effects
local ModelToVoice = {
    -- Police / Combine -> Radio Intercom effect
    ["models/player/police.mdl"] = eightbit.EFF_INTERCOM,
    ["models/player/combine_soldier.mdl"] = eightbit.EFF_INTERCOM,
    ["models/player/combine_super_soldier.mdl"] = eightbit.EFF_INTERCOM,

    -- Zombies -> Demon effect
    ["models/player/zombie_fast.mdl"] = eightbit.EFF_DEMON,
    ["models/player/charple.mdl"] = eightbit.EFF_DEMON,
    
    -- Alyx -> Robot effect (Example)
    ["models/player/alyx.mdl"] = eightbit.EFF_ROBOT,
}

-- Function to apply effect based on model
local function UpdatePlayerVoice(ply)
    if not IsValid(ply) then return end

    local model = string.lower(ply:GetModel() or "")
    local effectId = ModelToVoice[model]
    
    -- Important: Use EntIndex() (1-128), not UserID
    local entIndex = ply:EntIndex()

    if effectId then
        print("[Eightbit] Applying effect " .. effectId .. " to " .. ply:Nick())
        eightbit.EnableEffect(entIndex, effectId)
    else
        -- Disable effect if model is not in the list
        eightbit.EnableEffect(entIndex, eightbit.EFF_NONE)
    end
end

-- Hooks to update voice on spawn or model change
hook.Add("PlayerSpawn", "Eightbit_VoiceSpawn", function(ply)
    timer.Simple(0.1, function() if IsValid(ply) then UpdatePlayerVoice(ply) end end)
end)

hook.Add("PlayerSetModel", "Eightbit_VoiceModelChange", function(ply)
    timer.Simple(0.1, function() if IsValid(ply) then UpdatePlayerVoice(ply) end end)
end)

hook.Add("PlayerInitialSpawn", "Eightbit_VoiceInit", function(ply)
    timer.Simple(1, function() if IsValid(ply) then UpdatePlayerVoice(ply) end end)
end)

```

## Builds

Both Windows and Linux builds are available with every commit. See the [Actions page](https://www.google.com/search?q=https://github.com/your-repo/actions) to download the latest artifacts.