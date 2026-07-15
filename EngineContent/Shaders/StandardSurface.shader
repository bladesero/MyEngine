{
  "type": "Shader",
  "version": 2,
  "name": "Standard Surface",
  "mode": "Graph",
  "domain": "Surface",
  "shadingModel": "Lit",
  "surfaceType": "Opaque",
  "properties": [
    { "id": "surface.baseColor", "name": "Base Color", "type": "Color", "default": [1, 1, 1, 1], "sRGB": true },
    { "id": "surface.metallic", "name": "Metallic", "type": "Float", "default": 0, "range": [0, 1] },
    { "id": "surface.roughness", "name": "Roughness", "type": "Float", "default": 0.5, "range": [0.04, 1] },
    { "id": "surface.ambientOcclusion", "name": "Ambient Occlusion", "type": "Float", "default": 1, "range": [0, 1] },
    { "id": "surface.emissive", "name": "Emissive", "type": "Vec3", "default": [0, 0, 0] }
  ],
  "graph": {
    "version": 1,
    "nodes": [
      { "id": 10, "type": "Property", "property": "surface.baseColor", "position": [-420, -180], "pins": [{"id":11,"name":"Out","type":"Any","direction":"Output"}] },
      { "id": 20, "type": "Property", "property": "surface.metallic", "position": [-420, -50], "pins": [{"id":21,"name":"Out","type":"Any","direction":"Output"}] },
      { "id": 30, "type": "Property", "property": "surface.roughness", "position": [-420, 80], "pins": [{"id":31,"name":"Out","type":"Any","direction":"Output"}] },
      { "id": 40, "type": "Property", "property": "surface.ambientOcclusion", "position": [-420, 210], "pins": [{"id":41,"name":"Out","type":"Any","direction":"Output"}] },
      { "id": 50, "type": "Property", "property": "surface.emissive", "position": [-420, 340], "pins": [{"id":51,"name":"Out","type":"Any","direction":"Output"}] },
      { "id": 100, "type": "SurfaceOutputLit", "position": [80, 20], "pins": [
        {"id":101,"name":"BaseColor","type":"Any","direction":"Input"}, {"id":102,"name":"Normal","type":"Any","direction":"Input"},
        {"id":103,"name":"Metallic","type":"Any","direction":"Input"}, {"id":104,"name":"Roughness","type":"Any","direction":"Input"},
        {"id":105,"name":"AmbientOcclusion","type":"Any","direction":"Input"}, {"id":106,"name":"Emissive","type":"Any","direction":"Input"},
        {"id":107,"name":"Opacity","type":"Any","direction":"Input"}, {"id":108,"name":"AlphaClip","type":"Any","direction":"Input"}
      ]}
    ],
    "links": [
      {"id":200,"fromNode":10,"fromPin":"Out","fromPinId":11,"toNode":100,"toPin":"BaseColor","toPinId":101},
      {"id":201,"fromNode":20,"fromPin":"Out","fromPinId":21,"toNode":100,"toPin":"Metallic","toPinId":103},
      {"id":202,"fromNode":30,"fromPin":"Out","fromPinId":31,"toNode":100,"toPin":"Roughness","toPinId":104},
      {"id":203,"fromNode":40,"fromPin":"Out","fromPinId":41,"toNode":100,"toPin":"AmbientOcclusion","toPinId":105},
      {"id":204,"fromNode":50,"fromPin":"Out","fromPinId":51,"toNode":100,"toPin":"Emissive","toPinId":106}
    ]
  }
}
