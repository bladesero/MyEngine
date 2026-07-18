{
  "type": "Shader",
  "version": 1,
  "stages": {
    "vertex": { "source": "ModernGBuffer.hlsl", "entry": "VSMain" },
    "pixel": { "source": "ModernGBuffer.hlsl", "entry": "PSMain" }
  },
  "defines": ["MYENGINE_MODERN_GBUFFER=1"]
}
