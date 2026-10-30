#include "UI/Core/UIActorTreeBuilder.h"

#include "Core/Logger.h"
#include "Scene/Actor.h"
#include "UI/Core/UICanvasComponent.h"
#include "UI/Core/UIComponents.h"

#include <algorithm>
#include <iomanip>
#include <cmath>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace {

void HashCombine(std::size_t& seed, std::size_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
}

void HashString(std::size_t& seed, const std::string& value) {
    HashCombine(seed, std::hash<std::string>{}(value));
}

void HashFloat(std::size_t& seed, float value) {
    HashCombine(seed, std::hash<int>{}(static_cast<int>(value * 1000.0f)));
}

std::string EscapeText(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (char c : value) {
        switch (c) {
        case '&':
            result += "&amp;";
            break;
        case '<':
            result += "&lt;";
            break;
        case '>':
            result += "&gt;";
            break;
        default:
            result += c;
            break;
        }
    }
    return result;
}

std::string EscapeAttr(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (char c : value) {
        switch (c) {
        case '&':
            result += "&amp;";
            break;
        case '<':
            result += "&lt;";
            break;
        case '>':
            result += "&gt;";
            break;
        case '"':
            result += "&quot;";
            break;
        default:
            result += c;
            break;
        }
    }
    return result;
}

std::string Float(float value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(3) << value;
    return out.str();
}

int ColorByte(float value) {
    const float clamped = std::max(0.0f, std::min(1.0f, value));
    return static_cast<int>(std::round(clamped * 255.0f));
}

std::string ColorCss(const Color& color) {
    return "rgba(" + std::to_string(ColorByte(color.r)) + "," + std::to_string(ColorByte(color.g)) + "," +
           std::to_string(ColorByte(color.b)) + "," + std::to_string(ColorByte(color.a)) + ")";
}

std::string Dp(float value) {
    return Float(value) + "dp";
}

std::string Percent(float value) {
    return Float(value * 100.0f) + "%";
}

float Ratio(float value, float minValue, float maxValue) {
    if (maxValue <= minValue)
        return 0.0f;
    return std::clamp((value - minValue) / (maxValue - minValue), 0.0f, 1.0f);
}

void EnsurePositioned(std::string& style) {
    if (style.find("position:") == std::string::npos) {
        style += "position:relative;";
    }
}

void AppendClass(std::string& className, const char* value) {
    if (!className.empty())
        className += " ";
    className += value;
}

std::string RectStyle(const UIRectTransformComponent* rect) {
    if (!rect)
        return {};
    const RectTransform& r = rect->GetRect();
    std::string style = "position:absolute;";
    if (r.anchorMin.x == 0.0f && r.anchorMax.x == 1.0f) {
        style += "left:" + Dp(r.offsetMin.x) + ";";
        style += "right:" + Dp(-r.offsetMax.x) + ";";
    } else {
        style += "left:" + Float(r.anchorMin.x * 100.0f) + "%;";
        const float width = r.offsetMax.x - r.offsetMin.x;
        if (width > 0.0f)
            style += "width:" + Dp(width) + ";";
    }
    if (r.anchorMin.y == 0.0f && r.anchorMax.y == 1.0f) {
        style += "top:" + Dp(r.offsetMin.y) + ";";
        style += "bottom:" + Dp(-r.offsetMax.y) + ";";
    } else {
        style += "top:" + Float(r.anchorMin.y * 100.0f) + "%;";
        const float height = r.offsetMax.y - r.offsetMin.y;
        if (height > 0.0f)
            style += "height:" + Dp(height) + ";";
    }
    return style;
}

void AppendLayoutStyle(const Actor& actor, std::string& style, std::string& className) {
    const auto appendFlex = [&](const UILayoutGroupComponent& layout, const char* direction) {
        style += "display:flex;flex-direction:";
        style += direction;
        style += ";gap:" + Dp(layout.spacing) + ";padding:" + Dp(layout.padding) + ";";
        style += "align-items:" + layout.alignItems + ";justify-content:" + layout.justifyContent + ";";
    };
    if (auto* layout = actor.GetComponent<UIVerticalLayoutComponent>()) {
        appendFlex(*layout, "column");
        if (!layout->GetClassName().empty())
            className += " " + layout->GetClassName();
    } else if (auto* layout = actor.GetComponent<UIHorizontalLayoutComponent>()) {
        appendFlex(*layout, "row");
        if (!layout->GetClassName().empty())
            className += " " + layout->GetClassName();
    } else if (auto* grid = actor.GetComponent<UIGridLayoutComponent>()) {
        // RmlUi supports flex layout, not CSS grid. Rows/cells below implement equal-width column tracks without
        // unsupported grid-template-columns, fr units, or calc expressions.
        style += "display:flex;flex-direction:column;align-items:stretch;box-sizing:border-box;gap:" +
                 Dp(std::max(0.0f, grid->gap)) + ";padding:" + Dp(std::max(0.0f, grid->padding)) + ";";
        if (!grid->GetClassName().empty())
            className += " " + grid->GetClassName();
    }
}

void AppendGridItemStyle(const Actor& actor, std::string& style) {
    // Layout groups control their immediate children's placement and column width. Leaving RectStyle's absolute
    // anchors (or a widget's default 200dp width) in effect removes the child from flow and overlaps/overflows cells.
    style += "position:relative;left:auto;right:auto;top:auto;bottom:auto;box-sizing:border-box;"
             "width:100%;max-width:100%;min-width:0dp;flex-shrink:0;";
    if (!actor.GetComponent<UIVerticalLayoutComponent>() && !actor.GetComponent<UIHorizontalLayoutComponent>() &&
        !actor.GetComponent<UIGridLayoutComponent>()) {
        style += "display:block;"; // In particular, a span must accept the allocated cell width.
    }
    if (const auto* rect = actor.GetComponent<UIRectTransformComponent>()) {
        const RectTransform& r = rect->GetRect();
        const float height = r.offsetMax.y - r.offsetMin.y;
        if (!(r.anchorMin.y == 0.0f && r.anchorMax.y == 1.0f) && height > 0.0f)
            style += "height:" + Dp(height) + ";";
    }
}

const UIElementComponent* FindElementComponent(const Actor& actor) {
    if (auto* component = actor.GetComponent<UITextComponent>())
        return component;
    if (auto* component = actor.GetComponent<UIImageComponent>())
        return component;
    if (auto* component = actor.GetComponent<UIButtonComponent>())
        return component;
    if (auto* component = actor.GetComponent<UISliderComponent>())
        return component;
    if (auto* component = actor.GetComponent<UIProgressBarComponent>())
        return component;
    if (auto* component = actor.GetComponent<UIScrollViewComponent>())
        return component;
    if (auto* component = actor.GetComponent<UIVerticalLayoutComponent>())
        return component;
    if (auto* component = actor.GetComponent<UIHorizontalLayoutComponent>())
        return component;
    if (auto* component = actor.GetComponent<UIGridLayoutComponent>())
        return component;
    return nullptr;
}

std::string ElementIDFor(const Actor& actor, std::unordered_set<std::string>& usedIDs) {
    std::string id;
    if (const UIElementComponent* element = FindElementComponent(actor)) {
        id = element->GetElementID();
    }
    const std::string fallback = "ui_actor_" + std::to_string(actor.GetID());
    if (id.empty())
        id = fallback;
    if (usedIDs.insert(id).second)
        return id;
    Logger::Warn("[UI] Duplicate UI element id '", id, "' on actor ", actor.GetName(), "; using ", fallback);
    usedIDs.insert(fallback);
    return fallback;
}

void AppendActorElement(const Actor& actor, std::ostringstream& out, std::unordered_set<std::string>& usedIDs,
                        bool gridItem = false) {
    if (!actor.IsActiveSelf())
        return;

    std::string tag = "div";
    std::string content;
    std::string extraAttrs;
    std::string style = gridItem ? std::string{} : RectStyle(actor.GetComponent<UIRectTransformComponent>());
    std::string className;
    bool selfClosing = false;

    if (auto* text = actor.GetComponent<UITextComponent>()) {
        tag = "span";
        content = EscapeText(text->text);
        style += "font-size:" + Dp(text->fontSize) + ";color:" + ColorCss(text->color) + ";";
        className = text->GetClassName();
    } else if (auto* image = actor.GetComponent<UIImageComponent>()) {
        tag = "img";
        extraAttrs += " src=\"" + EscapeAttr(image->source) + "\"";
        style += "image-color:" + ColorCss(image->tint) + ";";
        className = image->GetClassName();
        selfClosing = true;
    } else if (auto* button = actor.GetComponent<UIButtonComponent>()) {
        tag = "button";
        content = EscapeText(button->text);
        style += "display:block;width:200dp;height:40dp;background-color:#2563eb;"
                 "border:0;border-radius:6dp;color:white;font-size:16dp;font-weight:bold;";
        if (button->disabled)
            extraAttrs += " disabled=\"disabled\"";
        className = button->GetClassName();
    } else if (auto* slider = actor.GetComponent<UISliderComponent>()) {
        tag = "div";
        const float valueRatio = Ratio(slider->value, slider->min, slider->max);
        extraAttrs += " data-ui-widget=\"slider\" data-min=\"" + Float(slider->min) + "\" data-max=\"" +
                      Float(slider->max) + "\" data-step=\"" + Float(slider->step) + "\" data-value=\"" +
                      Float(slider->value) + "\"";
        style += "display:block;width:200dp;height:24dp;";
        EnsurePositioned(style);
        content =
            "<div class=\"ui-slider-track\" style=\"position:absolute;left:0dp;right:0dp;top:9dp;height:6dp;"
            "background-color:#1f2937;border-radius:3dp;\"></div>"
            "<div class=\"ui-slider-fill\" style=\"position:absolute;left:0dp;top:9dp;width:" +
            Percent(valueRatio) +
            ";height:6dp;background-color:#3b82f6;border-radius:3dp;\"></div>"
            "<div class=\"ui-slider-handle\" style=\"position:absolute;left:" +
            Percent(valueRatio) +
            ";top:4dp;width:16dp;height:16dp;margin-left:-8dp;background-color:#e5e7eb;border-radius:8dp;\"></div>";
        if (!slider->dataBinding.empty()) {
            extraAttrs += " data-binding=\"" + EscapeAttr(slider->dataBinding) + "\"";
        }
        className = slider->GetClassName();
        AppendClass(className, "ui-slider");
    } else if (auto* progress = actor.GetComponent<UIProgressBarComponent>()) {
        tag = "div";
        const float valueRatio = Ratio(progress->value, 0.0f, progress->max);
        extraAttrs += " data-ui-widget=\"progress\" data-value=\"" + Float(progress->value) + "\" data-max=\"" +
                      Float(progress->max) + "\"";
        style += "display:block;width:200dp;height:16dp;background-color:#202938;"
                 "border-radius:5dp;overflow:hidden;";
        EnsurePositioned(style);
        content =
            "<div class=\"ui-progress-fill\" style=\"position:absolute;left:0dp;top:0dp;width:" + Percent(valueRatio) +
            ";height:100%;background-color:#22c55e;border-radius:5dp;\"></div>";
        className = progress->GetClassName();
        AppendClass(className, "ui-progress");
    } else if (auto* scroll = actor.GetComponent<UIScrollViewComponent>()) {
        tag = "div";
        style += "overflow-x:" + std::string(scroll->horizontal ? "auto" : "hidden") + ";";
        style += "overflow-y:" + std::string(scroll->vertical ? "auto" : "hidden") + ";";
        className = scroll->GetClassName();
    } else if (const UIElementComponent* element = FindElementComponent(actor)) {
        className = element->GetClassName();
    }

    AppendLayoutStyle(actor, style, className);
    if (gridItem)
        AppendGridItemStyle(actor, style);
    const std::string id = ElementIDFor(actor, usedIDs);
    out << "<" << tag << " id=\"" << EscapeAttr(id) << "\"";
    if (!className.empty())
        out << " class=\"" << EscapeAttr(className) << "\"";
    if (!style.empty())
        out << " style=\"" << EscapeAttr(style) << "\"";
    out << extraAttrs;

    if (selfClosing && actor.GetChildren().empty() && content.empty()) {
        out << " />\n";
        return;
    }

    out << ">";
    out << content;
    if (!actor.GetChildren().empty())
        out << "\n";
    const auto* grid = actor.GetComponent<UIGridLayoutComponent>();
    if (grid && !actor.GetComponent<UIVerticalLayoutComponent>() &&
        !actor.GetComponent<UIHorizontalLayoutComponent>()) {
        std::vector<const Actor*> visibleChildren;
        for (const Actor* child : actor.GetChildren()) {
            if (child && child->IsActiveSelf())
                visibleChildren.push_back(child);
        }
        const size_t columns = static_cast<size_t>(std::clamp(grid->columns, 1, 64));
        for (size_t first = 0; first < visibleChildren.size(); first += columns) {
            out << "<div class=\"ui-grid-row\" style=\"display:flex;flex-direction:row;align-items:stretch;"
                   "flex:0 0 auto;min-width:0dp;gap:"
                << Dp(std::max(0.0f, grid->gap)) << ";\">\n";
            for (size_t column = 0; column < columns; ++column) {
                out << "<div class=\"ui-grid-cell\" style=\"display:flex;flex-direction:column;align-items:stretch;"
                       "position:relative;flex:1 1 0dp;min-width:0dp;\">";
                if (first + column < visibleChildren.size())
                    AppendActorElement(*visibleChildren[first + column], out, usedIDs, true);
                // Empty final-row cells retain the same column widths as complete rows.
                out << "</div>\n";
            }
            out << "</div>\n";
        }
    } else {
        for (const Actor* child : actor.GetChildren()) {
            if (child)
                AppendActorElement(*child, out, usedIDs);
        }
    }
    out << "</" << tag << ">\n";
}

void HashActor(const Actor& actor, std::size_t& seed) {
    HashCombine(seed, actor.GetID());
    HashString(seed, actor.GetName());
    HashCombine(seed, actor.IsActiveSelf() ? 1 : 0);
    actor.ForEachComponent([&](Component& component) {
        HashString(seed, component.GetTypeName());
        HashCombine(seed, component.IsEnabled() ? 1 : 0);
        nlohmann::json data = nlohmann::json::object();
        component.Serialize(data);
        HashString(seed, data.dump());
    });
    for (const Actor* child : actor.GetChildren()) {
        if (child)
            HashActor(*child, seed);
    }
}

} // namespace

bool UIActorTreeBuilder::BuildDocument(const Actor& canvasActor, const UICanvasComponent& canvas, std::string& outRml,
                                       std::string* error) {
    (void)error;
    std::ostringstream out;
    out << "<rml>\n<head>\n<title>" << EscapeText(canvasActor.GetName()) << "</title>\n";
    for (const std::string& style : canvas.GetGeneratedStylePaths()) {
        if (!style.empty())
            out << "<link type=\"text/rcss\" href=\"" << EscapeAttr(style) << "\" />\n";
    }
    out << "</head>\n<body style=\"width:100%;height:100%;margin:0;"
           "font-family:LatoLatin;font-weight:normal;font-style:normal;"
           "font-size:16dp;color:#ffffff;\">\n";

    std::unordered_set<std::string> usedIDs;
    for (const Actor* child : canvasActor.GetChildren()) {
        if (child)
            AppendActorElement(*child, out, usedIDs);
    }

    out << "</body>\n</rml>\n";
    outRml = out.str();
    return true;
}

std::size_t UIActorTreeBuilder::ComputeSignature(const Actor& canvasActor) {
    std::size_t seed = 1469598103934665603ull;
    HashActor(canvasActor, seed);
    return seed;
}
