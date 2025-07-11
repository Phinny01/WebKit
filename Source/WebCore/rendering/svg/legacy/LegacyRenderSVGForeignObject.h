/*
 * Copyright (C) 2006 Apple Inc. All rights reserved.
 * Copyright (C) 2009 Google, Inc.
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
 */

#pragma once

#include "AffineTransform.h"
#include "FloatPoint.h"
#include "FloatRect.h"
#include "RenderSVGBlock.h"

namespace WebCore {

class SVGForeignObjectElement;

class LegacyRenderSVGForeignObject final : public RenderSVGBlock {
    WTF_MAKE_TZONE_OR_ISO_ALLOCATED(LegacyRenderSVGForeignObject);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(LegacyRenderSVGForeignObject);
public:
    LegacyRenderSVGForeignObject(SVGForeignObjectElement&, RenderStyle&&);
    virtual ~LegacyRenderSVGForeignObject();

    SVGForeignObjectElement& foreignObjectElement() const;

    void paint(PaintInfo&, const LayoutPoint&) override;

    bool requiresLayer() const override { return false; }
    void layout() override;

    FloatRect objectBoundingBox() const override { return FloatRect(FloatPoint(), m_viewport.size()); }
    bool isObjectBoundingBoxValid() const { return !m_viewport.isEmpty(); }
    FloatRect strokeBoundingBox() const override { return FloatRect(FloatPoint(), m_viewport.size()); }
    FloatRect repaintRectInLocalCoordinates(RepaintRectCalculation = RepaintRectCalculation::Fast) const override { return FloatRect(FloatPoint(), m_viewport.size()); }

    bool nodeAtFloatPoint(const HitTestRequest&, HitTestResult&, const FloatPoint& pointInParent, HitTestAction) override;

    void setNeedsTransformUpdate() override { m_needsTransformUpdate = true; }

private:
    void graphicsElement() const = delete;
    ASCIILiteral renderName() const override { return "RenderSVGForeignObject"_s; }

    void updateLogicalWidth() override;
    LogicalExtentComputedValues computeLogicalHeight(LayoutUnit logicalHeight, LayoutUnit logicalTop) const override;

    const AffineTransform& localToParentTransform() const override;
    AffineTransform localTransform() const override { return m_localTransform; }

    LayoutSize offsetFromContainer(const RenderElement&, const LayoutPoint&, bool* offsetDependsOnPoint = nullptr) const override;

    AffineTransform m_localTransform;
    mutable AffineTransform m_localToParentTransform;
    FloatRect m_viewport;
    bool m_needsTransformUpdate { true };
};

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_RENDER_OBJECT(LegacyRenderSVGForeignObject, isLegacyRenderSVGForeignObject())
