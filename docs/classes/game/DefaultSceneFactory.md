# DefaultSceneFactory

## Role

`DefaultSceneFactory` creates the built-in demo scene when a loaded scene is
empty.

## Responsibilities

- Populate Sun, PostProcess, Cube1, Cube2, ShadowPlane, and SkinnedCube actors.
- Register the checker texture and built-in demo materials.
- Bind Cube1 to `Content/Scripts/RotatingCube.as` with class `RotatingCube`.

Future template-asset work should replace this factory without changing
`SceneRenderLayer`.
