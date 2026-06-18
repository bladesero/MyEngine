#include "Editor/EditorLayout.h"

#include <algorithm>

EditorMainRects EditorLayout::Compute(float x, float y, float width, float height) {
    constexpr float toolbarHeight=40, outlinerWidth=280, inspectorWidth=320, logHeight=220;
    const float centerWidth = std::max(0.0f, width-outlinerWidth-inspectorWidth);
    const float contentHeight = std::max(0.0f, height-toolbarHeight-logHeight);
    return {{x,y,width,toolbarHeight},{x,y+toolbarHeight,outlinerWidth,contentHeight},
        {x+outlinerWidth,y+toolbarHeight,centerWidth,contentHeight},
        {x+width-inspectorWidth,y+toolbarHeight,inspectorWidth,height-toolbarHeight},
        {x,y+height-logHeight,outlinerWidth,logHeight},
        {x+outlinerWidth,y+height-logHeight,centerWidth,logHeight}};
}
