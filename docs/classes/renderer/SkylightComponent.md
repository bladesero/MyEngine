# SkylightComponent

## Role

`SkylightComponent` provides the scene-wide art controls for procedural sky and
indirect environment lighting. Its stable component type name is `Skylight` and
its schema version is 1.

Only enabled Skylights on active Actors participate. A scene is expected to have
at most one; if malformed scene or Prefab data contains more, scene traversal
order deterministically selects the first and the renderer emits one diagnostic
per conflict episode. With no effective Skylight, the scene root
`ambientIntensity` remains the compatibility fallback.

## Properties

- `environmentColor`: HDR tint applied to global and local-probe indirect light.
- `environmentIntensity`: final multiplier for global and local-probe indirect
  light. A newly added Editor component inherits the legacy ambient intensity.
- `skyIntensity`: visible procedural-sky background multiplier only.
- `skyTint`, `horizonTint`, `groundTint`: HDR procedural-sky regions used when
  generating the environment cubemap, mip chain, and spherical harmonics.

All color components and intensities are clamped to non-negative values. Values
above 1 are supported; the Inspector uses 0-20 as a soft editing range.

## Runtime behavior

The sun direction comes from the first enabled Directional Light, with the
existing renderer default used when none exists. The Skylight Actor transform is
not evaluated in schema version 1.

Changing the sun direction or any procedural-sky tint invalidates environment
generation. Changing environment color, environment intensity, or sky intensity
updates constants without regenerating the cubemap or SH. Forward, Classic
Deferred, and Modern Deferred share these rules.
