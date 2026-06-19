# Fix: set twoSided=true for all post-process pipelines to prevent
# back-face culling of the fullscreen triangle
path = r"src/Runtime/Renderer/PostProcessPass.cpp"
with open(path, "r", encoding="utf-8") as f:
    content = f.read()

# Fix 1: In EnsureResources (shader hot-reload path)
old1 = """            GraphicsPipelineDesc pipeline; pipeline.depthTest = false; pipeline.depthWrite = false;
            pipeline.shader = m_FXAAShader; pipeline.colorFormats = {RHIFormat::RGBA8UNorm};
            m_FXAABackbufferPipeline = device->CreateGraphicsPipeline(pipeline);
            pipeline.colorFormats = {RHIFormat::RGBA16Float};
            m_FXAAEditorPipeline = device->CreateGraphicsPipeline(pipeline);"""
new1 = """            GraphicsPipelineDesc pipeline; pipeline.depthTest = false; pipeline.depthWrite = false; pipeline.twoSided = true;
            pipeline.shader = m_FXAAShader; pipeline.colorFormats = {RHIFormat::RGBA8UNorm};
            m_FXAABackbufferPipeline = device->CreateGraphicsPipeline(pipeline);
            pipeline.colorFormats = {RHIFormat::RGBA16Float};
            m_FXAAEditorPipeline = device->CreateGraphicsPipeline(pipeline);"""
content = content.replace(old1, new1)

# Fix 2: Same pipelines created in the initial resource creation path
old2 = """    GraphicsPipelineDesc pipeline; pipeline.depthTest = false; pipeline.depthWrite = false;
    pipeline.shader = m_FXAAShader; pipeline.colorFormats = {RHIFormat::RGBA8UNorm};
    m_FXAABackbufferPipeline = device->CreateGraphicsPipeline(pipeline);
    pipeline.colorFormats = {RHIFormat::RGBA16Float};
    m_FXAAEditorPipeline = device->CreateGraphicsPipeline(pipeline);"""
new2 = """    GraphicsPipelineDesc pipeline; pipeline.depthTest = false; pipeline.depthWrite = false; pipeline.twoSided = true;
    pipeline.shader = m_FXAAShader; pipeline.colorFormats = {RHIFormat::RGBA8UNorm};
    m_FXAABackbufferPipeline = device->CreateGraphicsPipeline(pipeline);
    pipeline.colorFormats = {RHIFormat::RGBA16Float};
    m_FXAAEditorPipeline = device->CreateGraphicsPipeline(pipeline);"""
content = content.replace(old2, new2)

# Fix 3: SSAO pipeline
old3 = """            pipeline.shader = m_SSAOShader; pipeline.colorFormats = {RHIFormat::R8UNorm};
            m_SSAOPipeline = device->CreateGraphicsPipeline(pipeline);"""
new3 = """            pipeline.shader = m_SSAOShader; pipeline.colorFormats = {RHIFormat::R8UNorm};
            pipeline.twoSided = true;
            m_SSAOPipeline = device->CreateGraphicsPipeline(pipeline);"""
content = content.replace(old3, new3)

# Fix 4: SSAO pipeline (initial creation path)
old4 = """    pipeline.shader = m_SSAOShader; pipeline.colorFormats = {RHIFormat::R8UNorm};
    m_SSAOPipeline = device->CreateGraphicsPipeline(pipeline);"""
new4 = """    pipeline.shader = m_SSAOShader; pipeline.colorFormats = {RHIFormat::R8UNorm};
    pipeline.twoSided = true;
    m_SSAOPipeline = device->CreateGraphicsPipeline(pipeline);"""
content = content.replace(old4, new4)

# Fix 5: Blur pipeline
old5 = """    pipeline.shader = m_BlurShader;
    m_BlurPipeline = device->CreateGraphicsPipeline(pipeline);"""
new5 = """    pipeline.shader = m_BlurShader;
    pipeline.twoSided = true;
    m_BlurPipeline = device->CreateGraphicsPipeline(pipeline);"""
content = content.replace(old5, new5)

with open(path, "w", encoding="utf-8") as f:
    f.write(content)
print("Done - all 5 post-process pipelines now use twoSided=true")
