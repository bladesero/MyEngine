// Copy this file into Content/Editor/Scripts and rename
// RegisterEditorExamples to RegisterEditor to enable the sample.

void RegisterEditorExamples(EditorRegistry@ registry)
{
    registry.ToolPanel("project.audit", "Project Audit", Right, "DrawProjectAudit");
    registry.MenuItem("Tools/Project Audit", "DrawProjectAuditMenu");
    registry.ToolbarItem("project.audit.run", 100, "DrawProjectAuditToolbar");
    registry.InspectorSection("ScriptComponent", 100, "DrawScriptComponentExtras");
    registry.AssetContextMenu("*", "DrawAssetAuditContext");
    registry.ActorContextMenu("DrawActorAuditContext");
}

void DrawProjectAudit()
{
    UI::Text("Content root:");
    UI::Text(Project::GetContentRoot());

    if (UI::Button("Validate Materials"))
    {
        string materials = Assets::ListByType("Material");
        if (materials.length() == 0)
            Validation::ReportWarning("No material assets found.");
        else
            Validation::ReportInfo("Material assets found.");
    }
}

void DrawProjectAuditMenu()
{
    if (UI::MenuItem("Run Project Audit"))
    {
        Validation::ReportInfo("Project audit requested from menu.");
    }
}

void DrawProjectAuditToolbar()
{
    if (UI::Button("Audit"))
    {
        Validation::ReportInfo("Project audit requested from toolbar.");
    }
}

void DrawScriptComponentExtras()
{
    uint64 actorId = Selection::GetActorId();
    if (actorId == 0 || !Components::Has(actorId, "ScriptComponent"))
        return;

    UI::Separator();
    UI::Text("Script metadata:");
    UI::Text(Components::GetMetadata("ScriptComponent"));
}

void DrawAssetAuditContext()
{
    if (UI::MenuItem("Validate Selected Asset"))
    {
        Validation::ReportInfo("Asset validation requested: " + Selection::GetAssetPath());
    }
}

void DrawActorAuditContext()
{
    if (UI::MenuItem("Validate Selected Actor"))
    {
        Validation::ReportInfo("Actor validation requested.");
    }
}
