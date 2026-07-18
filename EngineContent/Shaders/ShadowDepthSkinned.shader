{
  "type": "Shader",
  "version": 1,
  "stages": {
    "vertex": { "source": "ShadowDepth.hlsl", "entry": "VSMain" },
    "pixel": { "source": "ShadowDepth.hlsl", "entry": "PSMain" }
  },
  "defines": ["MYENGINE_SHADOW_SKINNED=1"]
}
