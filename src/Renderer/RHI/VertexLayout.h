#pragma once

// API-agnostic input layout declarations.
enum class VertexFormat { Float2, Float3, Float4 };

struct VertexElement {
    const char* semantic;    // e.g. "POSITION", "COLOR"
    unsigned int index;      // semantic index
    VertexFormat format;
    unsigned int offset;     // byte offset inside the vertex struct
};
