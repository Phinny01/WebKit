/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 * Copyright (C) 2000 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2004-2022 Apple Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include "config.h"
#include "RenderWidget.h"

#include "AXObjectCache.h"
#include "BackgroundPainter.h"
#include "DocumentInlines.h"
#include "FloatRoundedRect.h"
#include "HTMLFrameOwnerElement.h"
#include "HitTestResult.h"
#include "LocalFrame.h"
#include "RemoteFrame.h"
#include "RemoteFrameView.h"
#include "RenderBox.h"
#include "RenderBoxInlines.h"
#include "RenderElementInlines.h"
#include "RenderEmbeddedObject.h"
#include "RenderLayer.h"
#include "RenderLayerBacking.h"
#include "RenderLayerScrollableArea.h"
#include "RenderObjectInlines.h"
#include "RenderView.h"
#include "RenderWidgetInlines.h"
#include "SecurityOrigin.h"
#include <wtf/Ref.h>
#include <wtf/StackStats.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(RenderWidget);

static HashMap<SingleThreadWeakRef<const Widget>, SingleThreadWeakRef<RenderWidget>>& widgetRendererMap()
{
    static NeverDestroyed<HashMap<SingleThreadWeakRef<const Widget>, SingleThreadWeakRef<RenderWidget>>> staticWidgetRendererMap;
    return staticWidgetRendererMap;
}

unsigned WidgetHierarchyUpdatesSuspensionScope::s_widgetHierarchyUpdateSuspendCount = 0;
bool WidgetHierarchyUpdatesSuspensionScope::s_haveScheduledWidgetToMove = false;

WidgetHierarchyUpdatesSuspensionScope::WidgetToParentMap& WidgetHierarchyUpdatesSuspensionScope::widgetNewParentMap()
{
    static NeverDestroyed<WidgetToParentMap> map;
    return map;
}

void WidgetHierarchyUpdatesSuspensionScope::moveWidgets()
{
    ASSERT(s_haveScheduledWidgetToMove);
    while (!widgetNewParentMap().isEmpty()) {
        auto map = std::exchange(widgetNewParentMap(), { });
        for (auto& entry : map) {
            auto& child = *entry.key;
            auto* currentParent = child.parent();
            CheckedPtr newParent = entry.value.get();
            if (newParent != currentParent) {
                if (currentParent)
                    currentParent->removeChild(child);
                if (newParent)
                    newParent->addChild(child);
            }
        }
    }
    s_haveScheduledWidgetToMove = false;
}

static void moveWidgetToParentSoon(Widget& child, LocalFrameView* parent)
{
    if (!WidgetHierarchyUpdatesSuspensionScope::isSuspended()) {
        if (parent)
            parent->addChild(child);
        else
            child.removeFromParent();
        return;
    }
    WidgetHierarchyUpdatesSuspensionScope::scheduleWidgetToMove(child, parent);
}

RenderWidget::RenderWidget(Type type, HTMLFrameOwnerElement& element, RenderStyle&& style)
    : RenderReplaced(type, element, WTFMove(style), ReplacedFlag::IsWidget)
{
    relaxAdoptionRequirement();
    setInline(false);
}

void RenderWidget::willBeDestroyed()
{
    if (CheckedPtr cache = document().existingAXObjectCache()) {
        if (auto* parent = this->parent())
            cache->childrenChanged(*parent);
        cache->remove(*this);
    }

    if (renderTreeBeingDestroyed() && document().backForwardCacheState() == Document::NotInBackForwardCache && m_widget)
        m_widget->willBeDestroyed();

    setWidget(nullptr);

    RenderReplaced::willBeDestroyed();
}

RenderWidget::~RenderWidget() = default;

// Widgets are always placed on integer boundaries, so rounding the size is actually
// the desired behavior. This function is here because it's otherwise seldom what we
// want to do with a LayoutRect.
static inline IntRect roundedIntRect(const LayoutRect& rect)
{
    return IntRect(roundedIntPoint(rect.location()), roundedIntSize(rect.size()));
}

bool RenderWidget::setWidgetGeometry(const LayoutRect& frame)
{
    IntRect clipRect = roundedIntRect(enclosingLayer()->childrenClipRect());
    IntRect newFrameRect = roundedIntRect(frame);
    IntRect oldFrameRect = m_widget->frameRect();
    bool clipChanged = m_clipRect != clipRect;
    bool boundsChanged = oldFrameRect != newFrameRect;

    if (!boundsChanged && !clipChanged)
        return false;

    m_clipRect = clipRect;

    WeakPtr weakThis { *this };
    // These calls *may* cause this renderer to disappear from underneath...
    if (boundsChanged)
        m_widget->setFrameRect(newFrameRect);
    else if (clipChanged)
        m_widget->clipRectChanged();
    // ...so we follow up with a sanity check.
    if (!weakThis)
        return true;

    if (boundsChanged)
        view().compositor().widgetDidChangeSize(*this);

    return oldFrameRect.size() != newFrameRect.size();
}

bool RenderWidget::updateWidgetGeometry()
{
    if (!m_widget->transformsAffectFrameRect())
        return setWidgetGeometry(absoluteContentBox());

    LayoutRect contentBox = contentBoxRect();
    LayoutRect absoluteContentBox(localToAbsoluteQuad(FloatQuad(contentBox)).boundingBox());
    if (is<FrameView>(m_widget)) {
        contentBox.setLocation(absoluteContentBox.location());
        return setWidgetGeometry(contentBox);
    }

    return setWidgetGeometry(absoluteContentBox);
}

void RenderWidget::setWidget(RefPtr<Widget>&& widget)
{
    if (widget == m_widget)
        return;

    if (is<RemoteFrameView>(m_widget) != is<RemoteFrameView>(widget))
        frameOwnerElement().scheduleInvalidateStyleAndLayerComposition();

    if (m_widget) {
        moveWidgetToParentSoon(*m_widget, nullptr);
        view().frameView().willRemoveWidgetFromRenderTree(*m_widget);
        widgetRendererMap().remove(*m_widget);
        m_widget = nullptr;
    }
    m_widget = widget;
    if (m_widget) {
        widgetRendererMap().add(*m_widget, *this);
        view().frameView().didAddWidgetToRenderTree(*m_widget);
        // If we've already received a layout, apply the calculated space to the
        // widget immediately, but we have to have really been fully constructed.
        if (hasInitializedStyle()) {
            if (!needsLayout()) {
                WeakPtr weakThis { *this };
                updateWidgetGeometry();
                if (!weakThis)
                    return;
            }

            if (style().usedVisibility() != Visibility::Visible)
                m_widget->hide();
            else {
                m_widget->show();
                repaint();
            }
            if (CheckedPtr cache = document().existingAXObjectCache())
                cache->onWidgetVisibilityChanged(*this);
        }
        moveWidgetToParentSoon(*m_widget, &view().frameView());
    }

    if (CheckedPtr cache = document().existingAXObjectCache())
        cache->childrenChanged(*this);
}

void RenderWidget::layout()
{
    StackStats::LayoutCheckPoint layoutCheckPoint;
    ASSERT(needsLayout());

    clearNeedsLayout();
}

void RenderWidget::styleDidChange(StyleDifference diff, const RenderStyle* oldStyle)
{
    RenderReplaced::styleDidChange(diff, oldStyle);
    if (m_widget) {
        if (style().usedVisibility() != Visibility::Visible)
            m_widget->hide();
        else
            m_widget->show();

        if (CheckedPtr cache = document().existingAXObjectCache())
            cache->onWidgetVisibilityChanged(*this);
    }
}

void RenderWidget::paintContents(PaintInfo& paintInfo, const LayoutPoint& paintOffset)
{
    ASSERT(!isSkippedContentRoot(*this));

    if (paintInfo.requireSecurityOriginAccessForWidgets) {
        if (auto contentDocument = frameOwnerElement().contentDocument()) {
            if (!document().protectedSecurityOrigin()->isSameOriginDomain(contentDocument->securityOrigin()))
                return;
        }
    }

    auto contentPaintOffset = paintOffset + location() + contentBoxRect().location();
    auto snappedPaintOffset = roundPointToDevicePixels(contentPaintOffset, document().deviceScaleFactor());

    // Tell the widget to paint now. This is the only time the widget is allowed
    // to paint itself. That way it will composite properly with z-indexed layers.
    LayoutRect paintRect = paintInfo.rect;

    OptionSet<PaintBehavior> oldBehavior = PaintBehavior::Normal;
    if (paintInfo.paintBehavior & PaintBehavior::DefaultAsynchronousImageDecode) {
        if (RefPtr frameView = dynamicDowncast<LocalFrameView>(m_widget)) {
            oldBehavior = frameView->paintBehavior();
            frameView->setPaintBehavior(oldBehavior | PaintBehavior::DefaultAsynchronousImageDecode);
        }
    }

    auto widgetLocation = m_widget->frameRect().location();
    auto widgetPaintOffset = snappedPaintOffset - widgetLocation;
    // When painting widgets into compositing layers, tx and ty are relative to the enclosing compositing layer,
    // not the root. In this case, shift the CTM and adjust the paintRect to be root-relative to fix plug-in drawing.
    if (!widgetPaintOffset.isZero()) {
        paintInfo.context().translate(widgetPaintOffset);
        paintRect.move(-widgetPaintOffset.width(), -widgetPaintOffset.height());
    }

    if (paintInfo.regionContext) {
        AffineTransform transform;
        transform.translate(snappedPaintOffset);
        paintInfo.regionContext->pushTransform(transform);
    }

    // FIXME: Remove repaintrect enclosing/integral snapping when RenderWidget becomes device pixel snapped.
    m_widget->paint(paintInfo.context(), enclosingIntRect(paintRect), paintInfo.requireSecurityOriginAccessForWidgets ? Widget::SecurityOriginPaintPolicy::AccessibleOriginOnly : Widget::SecurityOriginPaintPolicy::AnyOrigin, paintInfo.regionContext);

    if (paintInfo.regionContext)
        paintInfo.regionContext->popTransform();

    if (!widgetPaintOffset.isZero())
        paintInfo.context().translate(-widgetPaintOffset);

    if (RefPtr frameView = dynamicDowncast<LocalFrameView>(m_widget)) {
        bool runOverlapTests = !frameView->useSlowRepaintsIfNotOverlapped();
        if (paintInfo.overlapTestRequests && runOverlapTests) {
            ASSERT(!paintInfo.overlapTestRequests->contains(this) || (paintInfo.overlapTestRequests->get(this) == m_widget->frameRect()));
            paintInfo.overlapTestRequests->set(this, m_widget->frameRect());
        }
        if (paintInfo.paintBehavior & PaintBehavior::DefaultAsynchronousImageDecode)
            frameView->setPaintBehavior(oldBehavior);
    }
}

void RenderWidget::paint(PaintInfo& paintInfo, const LayoutPoint& paintOffset)
{
    if (!shouldPaint(paintInfo, paintOffset))
        return;

    if (paintInfo.context().detectingContentfulPaint())
        return;

    LayoutPoint adjustedPaintOffset = paintOffset + location();

    if (hasVisibleBoxDecorations() && (paintInfo.phase == PaintPhase::Foreground || paintInfo.phase == PaintPhase::Selection))
        paintBoxDecorations(paintInfo, adjustedPaintOffset);

    if (paintInfo.phase == PaintPhase::Mask) {
        paintMask(paintInfo, adjustedPaintOffset);
        return;
    }

    if ((paintInfo.phase == PaintPhase::Outline || paintInfo.phase == PaintPhase::SelfOutline) && hasOutline())
        paintOutline(paintInfo, LayoutRect(adjustedPaintOffset, size()));

    // FIXME: Shouldn't check if the frame view needs layout during event region painting. This is a workaround
    // for the fact that non-composited frames depend on their enclosing compositing layer to perform an event
    // region update on their behalf. See <https://webkit.org/b/210311> for more details.
    auto* frameView = dynamicDowncast<LocalFrameView>(m_widget.get());
    bool needsEventRegionContentPaint = paintInfo.phase == PaintPhase::EventRegion && frameView && !frameView->needsLayout();
    if (paintInfo.phase != PaintPhase::Foreground && !needsEventRegionContentPaint)
        return;

    if (style().hasBorderRadius()) {
        LayoutRect borderRect = LayoutRect(adjustedPaintOffset, size());

        if (borderRect.isEmpty())
            return;

        // Push a clip if we have a border radius, since we want to round the foreground content that gets painted.
        paintInfo.context().save();
        clipToContentBoxShape(paintInfo.context(), adjustedPaintOffset, document().deviceScaleFactor());
    }

    if (m_widget && !isSkippedContentRoot(*this))
        paintContents(paintInfo, paintOffset);

    if (style().hasBorderRadius())
        paintInfo.context().restore();

    if (paintInfo.phase == PaintPhase::EventRegion || paintInfo.phase == PaintPhase::Accessibility)
        return;

    // Paint a partially transparent wash over selected widgets.
    if (isSelected() && !document().printing()) {
        LayoutRect rect = localSelectionRect();
        rect.moveBy(adjustedPaintOffset);
        paintInfo.context().fillRect(snappedIntRect(rect), selectionBackgroundColor());
    }

    if (hasLayer() && layer()->canResize()) {
        ASSERT(layer()->scrollableArea());
        layer()->scrollableArea()->paintResizer(paintInfo.context(), roundedIntPoint(adjustedPaintOffset), paintInfo.rect);
    }
}

void RenderWidget::setOverlapTestResult(bool isOverlapped)
{
    ASSERT(m_widget);
    downcast<LocalFrameView>(*m_widget).setIsOverlapped(isOverlapped);
}

RenderWidget::ChildWidgetState RenderWidget::updateWidgetPosition()
{
    if (!m_widget)
        return ChildWidgetState::Destroyed;

    WeakPtr weakThis { *this };
    bool widgetSizeChanged = updateWidgetGeometry();
    if (!weakThis || !m_widget)
        return ChildWidgetState::Destroyed;

    // if the frame size got changed, or if view needs layout (possibly indicating
    // content size is wrong) we have to do a layout to set the right widget size.
    if (RefPtr frameView = dynamicDowncast<LocalFrameView>(*m_widget)) {
        // Check the frame's page to make sure that the frame isn't in the process of being destroyed.
        Ref localFrame = frameView->frame();
        if ((widgetSizeChanged || frameView->needsLayout())
            && localFrame->page()
            && localFrame->document())
            frameView->layoutContext().layout();
    }
    return ChildWidgetState::Valid;
}

IntRect RenderWidget::windowClipRect() const
{
    Ref frameView = view().frameView();
    return intersection(frameView->contentsToWindow(m_clipRect), frameView->windowClipRect());
}

void RenderWidget::setSelectionState(HighlightState state)
{
    // The selection state for our containing block hierarchy is updated by the base class call.
    RenderReplaced::setSelectionState(state);

    if (m_widget)
        m_widget->setIsSelected(isSelected());
}

RenderWidget* RenderWidget::find(const Widget& widget)
{
    return widgetRendererMap().get(&widget);
}

bool RenderWidget::nodeAtPoint(const HitTestRequest& request, HitTestResult& result, const HitTestLocation& locationInContainer, const LayoutPoint& accumulatedOffset, HitTestAction action)
{
    auto shouldHitTestChildFrameContent = request.allowsChildFrameContent() || (request.allowsVisibleChildFrameContent() && visibleToHitTesting(request));
    auto* childFrameView = dynamicDowncast<LocalFrameView>(widget());
    if (shouldHitTestChildFrameContent && childFrameView && childFrameView->renderView()) {
        LayoutPoint adjustedLocation = accumulatedOffset + location();
        LayoutPoint contentOffset = LayoutPoint(borderLeft() + paddingLeft(), borderTop() + paddingTop()) - toIntSize(childFrameView->scrollPosition());
        HitTestLocation newHitTestLocation(locationInContainer, -adjustedLocation - contentOffset);
        HitTestRequest newHitTestRequest(request.type() | HitTestRequest::Type::ChildFrameHitTest);
        HitTestResult childFrameResult(newHitTestLocation);

        auto* document = childFrameView->frame().document();
        if (!document)
            return false;
        bool isInsideChildFrame = document->hitTest(newHitTestRequest, newHitTestLocation, childFrameResult);

        if (request.resultIsElementList())
            result.append(childFrameResult, request);
        else if (isInsideChildFrame)
            result = childFrameResult;

        if (isInsideChildFrame)
            return true;
    }

    bool hadResult = result.innerNode();
    bool inside = RenderReplaced::nodeAtPoint(request, result, locationInContainer, accumulatedOffset, action);

    // Check to see if we are really over the widget itself (and not just in the border/padding area).
    if ((inside || result.isRectBasedTest()) && !hadResult && result.innerNode() == &frameOwnerElement())
        result.setIsOverWidget(contentBoxRect().contains(result.localPoint()));
    return inside;
}

bool RenderWidget::requiresLayer() const
{
    return RenderReplaced::requiresLayer() || requiresAcceleratedCompositing();
}

bool RenderWidget::requiresAcceleratedCompositing() const
{
    // If this is a renderer with a contentDocument and that document needs a layer, then we need a layer.
    if (auto* contentDocument = frameOwnerElement().contentDocument()) {
        if (RenderView* view = contentDocument->renderView())
            return view->usesCompositing();
    }

    if (is<RemoteFrameView>(widget()))
        return true;

    return false;
}

RemoteFrame* RenderWidget::remoteFrame() const
{
    return dynamicDowncast<RemoteFrame>(frameOwnerElement().contentFrame());
}

bool RenderWidget::shouldInvalidatePreferredWidths() const
{
    if (RenderReplaced::shouldInvalidatePreferredWidths())
        return true;
    return embeddedContentBox();
}

RenderBox* RenderWidget::embeddedContentBox() const
{
    if (!is<RenderEmbeddedObject>(this))
        return nullptr;
    RefPtr frameView = dynamicDowncast<LocalFrameView>(widget());
    return frameView ? frameView->embeddedContentBox() : nullptr;
}

} // namespace WebCore
