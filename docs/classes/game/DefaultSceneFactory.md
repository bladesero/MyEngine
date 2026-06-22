# DefaultSceneFactory

## Role

`DefaultSceneFactory` is a legacy hook called after scene load. It no longer
creates built-in actors or demo content.

## Responsibilities

- Leave empty scenes empty.
- Leave authored scenes unchanged.
- Preserve a narrow replacement point for future explicit scene templates.

Scene startup content should come from saved scene assets or user actions in
the editor, not from implicit runtime population.
