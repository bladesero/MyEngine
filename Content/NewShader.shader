{
  "domain": "Surface",
  "graph": {
    "links": [
      {
        "fromNode": 10,
        "fromPin": "Out",
        "fromPinId": 11,
        "id": 200,
        "toNode": 100,
        "toPin": "BaseColor",
        "toPinId": 101
      },
      {
        "fromNode": 20,
        "fromPin": "Out",
        "fromPinId": 21,
        "id": 201,
        "toNode": 100,
        "toPin": "Metallic",
        "toPinId": 103
      },
      {
        "fromNode": 30,
        "fromPin": "Out",
        "fromPinId": 31,
        "id": 202,
        "toNode": 100,
        "toPin": "Roughness",
        "toPinId": 104
      }
    ],
    "nodes": [
      {
        "id": 10,
        "pins": [
          {
            "direction": "Output",
            "id": 11,
            "name": "Out",
            "type": "Any"
          }
        ],
        "position": [
          -220.0,
          149.0
        ],
        "property": "surface.baseColor",
        "type": "Property"
      },
      {
        "id": 20,
        "pins": [
          {
            "direction": "Output",
            "id": 21,
            "name": "Out",
            "type": "Any"
          }
        ],
        "position": [
          -196.0,
          289.0
        ],
        "property": "surface.metallic",
        "type": "Property"
      },
      {
        "id": 30,
        "pins": [
          {
            "direction": "Output",
            "id": 31,
            "name": "Out",
            "type": "Any"
          }
        ],
        "position": [
          -242.0,
          427.0
        ],
        "property": "surface.roughness",
        "type": "Property"
      },
      {
        "id": 100,
        "pins": [
          {
            "default": [
              1.0,
              1.0,
              1.0,
              1.0
            ],
            "direction": "Input",
            "id": 101,
            "name": "BaseColor",
            "type": "Color"
          },
          {
            "default": [
              0.0,
              0.0,
              1.0
            ],
            "direction": "Input",
            "id": 102,
            "name": "Normal",
            "type": "Vec3"
          },
          {
            "default": [
              0.0
            ],
            "direction": "Input",
            "id": 103,
            "name": "Metallic",
            "type": "Float"
          },
          {
            "default": [
              0.5
            ],
            "direction": "Input",
            "id": 104,
            "name": "Roughness",
            "type": "Float"
          },
          {
            "default": [
              1.0
            ],
            "direction": "Input",
            "id": 105,
            "name": "AmbientOcclusion",
            "type": "Float"
          },
          {
            "default": [
              0.0,
              0.0,
              0.0,
              0.0
            ],
            "direction": "Input",
            "id": 106,
            "name": "Emissive",
            "type": "Color"
          },
          {
            "default": [
              1.0
            ],
            "direction": "Input",
            "id": 107,
            "name": "Opacity",
            "type": "Float"
          },
          {
            "default": [
              0.5
            ],
            "direction": "Input",
            "id": 108,
            "name": "AlphaClip",
            "type": "Float"
          }
        ],
        "position": [
          1722.0,
          225.0
        ],
        "type": "SurfaceOutputLit"
      }
    ],
    "version": 2
  },
  "mode": "Graph",
  "name": "NewShader",
  "properties": [
    {
      "default": [
        1.0,
        1.0,
        1.0,
        1.0
      ],
      "id": "surface.baseColor",
      "name": "Base Color",
      "sRGB": true,
      "type": "Color"
    },
    {
      "default": [
        0.0
      ],
      "id": "surface.metallic",
      "name": "Metallic",
      "range": [
        0.0,
        1.0
      ],
      "type": "Float"
    },
    {
      "default": [
        0.5
      ],
      "id": "surface.roughness",
      "name": "Roughness",
      "range": [
        0.03999999910593033,
        1.0
      ],
      "type": "Float"
    }
  ],
  "shadingModel": "Lit",
  "surfaceType": "Opaque",
  "type": "Shader",
  "version": 2
}
