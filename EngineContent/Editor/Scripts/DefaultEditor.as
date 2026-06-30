void RegisterEditor(EditorRegistry@ registry)
{
    registry.PanelBody("toolbar", "DrawToolbar");
    registry.PanelBody("log", "DrawLog");
    registry.PanelBody("profiler", "DrawProfiler");

    registry.Menu("File/New", "scene.new");
    registry.Menu("File/Open", "scene.open");
    registry.Menu("File/Save", "scene.save");
}

void DrawToolbar()
{
    UI::ToolbarActionButtonEx("play.start", "play-start", 1, true);
    UI::ToolbarActionButtonEx("play.stop", "play-stop", 2, true);
    UI::ToolbarActionButtonEx("play.pause", "play-pause", 3, true);
    UI::ToolbarActionButtonEx("play.step", "play-step", 0, false);
}

void DrawLog()
{
    if (UI::Button("Clear")) {
        Log::Clear();
    }
    UI::SameLine();
    bool autoScroll = Log::GetAutoScroll();
    if (UI::Checkbox("Auto-scroll", autoScroll)) {
        Log::SetAutoScroll(autoScroll);
    }
    UI::Separator();
    UI::BeginChild("##ScriptLogScroll", 0, 0, false);
    UI::Text(Log::GetRows());
    UI::EndChild();
}

void DrawProfiler()
{
    bool enabled = Profiler::GetEnabled();
    if (UI::Checkbox("Record", enabled)) {
        Profiler::SetEnabled(enabled);
    }
    UI::SameLine();
    if (UI::Button("Clear")) {
        Profiler::Clear();
    }
    UI::Separator();
    UI::Text(Profiler::GetFrameStats());
    UI::Separator();
    UI::BeginChild("##ScriptProfilerRows", 0, 0, false);
    UI::Text(Profiler::GetRows());
    UI::EndChild();
}
