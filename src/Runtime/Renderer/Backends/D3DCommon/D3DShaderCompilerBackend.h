#pragma once

// Called by RuntimeModule's explicit composition root. The backend owns all
// native D3D compiler implementation details.
bool RegisterD3DShaderCompilerBackend();
