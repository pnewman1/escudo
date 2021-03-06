/*
 * Copyright 2010-2013 Esrille Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Box.h"

#include <algorithm>
#include <new>
#include <iostream>

#include <boost/bind.hpp>

#include <Object.h>
#include <org/w3c/dom/Document.h>
#include <org/w3c/dom/Element.h>
#include <org/w3c/dom/Text.h>

#include "BoxImage.h"
#include "CSSSerialize.h"
#include "CSSStyleDeclarationImp.h"
#include "CSSTokenizer.h"
#include "DocumentImp.h"
#include "FormattingContext.h"
#include "StackingContext.h"
#include "ViewCSSImp.h"
#include "WindowImp.h"

#include "Table.h"

#include "html/HTMLTemplateElementImp.h"    // TODO: only for XBL2

#include "Test.util.h"

namespace org { namespace w3c { namespace dom { namespace bootstrap {

Box::Box(Node node) :
    node(node),
    parentBox(0),
    firstChild(0),
    lastChild(0),
    previousSibling(0),
    nextSibling(0),
    childCount(0),
    clearance(NAN),
    marginTop(0.0f),
    marginRight(0.0f),
    marginBottom(0.0f),
    marginLeft(0.0f),
    paddingTop(0.0f),
    paddingRight(0.0f),
    paddingBottom(0.0f),
    paddingLeft(0.0f),
    borderTop(0.0f),
    borderRight(0.0f),
    borderBottom(0.0f),
    borderLeft(0.0f),
    position(CSSPositionValueImp::Static),
    offsetH(0.0f),
    offsetV(0.0f),
    visibility(CSSVisibilityValueImp::Visible),
    stackingContext(0),
    nextBase(0),
    nextFloat(0),
    intrinsic(false),
    x(0.0f),
    y(0.0f),
    clipBox(0),
    backgroundColor(0x00000000),
    backgroundRequest(0),
    backgroundImage(0),
    backgroundLeft(0.0f),
    backgroundTop(0.0f),
    backgroundStart(getTick()),
    style(0),
    flags(0),
    childWindow(0)
{
}

Box::~Box()
{
    delete backgroundRequest;

    if (stackingContext)
        stackingContext->removeBox(this);

    while (0 < childCount) {
        Box* child = removeChild(firstChild);
        child->release_();
    }

    if (style)
        style->removeBox(this);
}

Box* Box::removeChild(Box* item)
{
    Box* next = item->nextSibling;
    Box* prev = item->previousSibling;
    if (!next)
        lastChild = prev;
    else
        next->previousSibling = prev;
    if (!prev)
        firstChild = next;
    else
        prev->nextSibling = next;
    item->parentBox = item->previousSibling = item->nextSibling = 0;
    --childCount;

    if (auto block = dynamic_cast<Block*>(item))
        block->inserted = false;
    else if (item->style)
        item->style->removeBox(item);

    return item;
}

Box* Box::insertBefore(Box* item, Box* after)
{
    assert(item != parentBox);
    if (!after)
        return appendChild(item);
    item->previousSibling = after->previousSibling;
    item->nextSibling = after;
    after->previousSibling = item;
    if (!item->previousSibling)
        firstChild = item;
    else
        item->previousSibling->nextSibling = item;
    item->parentBox = this;
    item->retain_();
    ++childCount;
    return item;
}

Box* Box::appendChild(Box* item)
{
    assert(item != parentBox);
    Box* prev = lastChild;
    if (!prev)
        firstChild = item;
    else
        prev->nextSibling = item;
    item->previousSibling = prev;
    item->nextSibling = 0;
    lastChild = item;
    item->parentBox = this;
    item->retain_();
    ++childCount;
    return item;
}

void Box::removeChildren()
{
    while (hasChildBoxes()) {
        Box* child = getFirstChild();
        removeChild(child);
        child->release_();
    }
}

void Box::removeDescendants()
{
    auto block = dynamic_cast<Block*>(this);
    if (block)
        block->clearBlocks();
    while (hasChildBoxes()) {
        Box* child = getFirstChild();
        removeChild(child);
        child->removeDescendants();
        child->release_();
    }
    if (block)
        block->clearInlines();
    if (style)
        style->removeBox(this);
}

void Box::setStyle(CSSStyleDeclarationImp* style)
{
    this->style = style;
    if (style) {
        stackingContext = style->getStackingContext();
        if (!isAnonymous())
            setPosition(style->getUsedPosition());
    }
}

void Box::unresolveStyle()
{
    if (!isAnonymous()) {
        if (CSSStyleDeclarationImp* style = getStyle())
            style->unresolve();
    }
    for (Box* i = firstChild; i; i = i->nextSibling)
        i->unresolveStyle();
}

float Box::getOutlineWidth() const
{
    if (CSSStyleDeclarationImp* style = getStyle())
        return style->outlineWidth.getPx();
    return 0.0f;
}

float Box::getEffectiveTotalWidth() const
{
    // cf. http://test.csswg.org/suites/css2.1/20110323/html4/clear-float-002.htm
    float c = getClearance();
    float h = getTotalHeight();
    if (LineBox* lineBox = dynamic_cast<LineBox*>(parentBox))
        c += lineBox->getClearance();
    if (0.0f < c)
        h += c;
    return (h <= 0.0f) ? 0.0f : getTotalWidth();
}

void Box::updatePadding()
{
    paddingTop = style->paddingTop.getPx();
    paddingRight = style->paddingRight.getPx();
    paddingBottom = style->paddingBottom.getPx();
    paddingLeft = style->paddingLeft.getPx();
}

void Box::updateBorderWidth()
{
    borderTop = style->borderTopWidth.getPx();
    borderRight = style->borderRightWidth.getPx();
    borderBottom = style->borderBottomWidth.getPx();
    borderLeft = style->borderLeftWidth.getPx();
}

const ContainingBlock* Box::getContainingBlock(ViewCSSImp* view) const
{
    const Box* box = this;
    do {
        const Box* parent = box->getParentBox();
        if (!parent)
            return view->getInitialContainingBlock();
        box = parent;
    } while (box->getBoxType() != BLOCK_LEVEL_BOX);
    return box;
}

Element Box::getContainingElement(Node node)
{
    for (; node; node = node.getParentNode()) {
        if (node.getNodeType() == Node::ELEMENT_NODE) {
            if (auto shadowTree = dynamic_cast<HTMLTemplateElementImp*>(node.self()))
                node = shadowTree->getHost();
            return interface_cast<Element>(node);
        }
    }
    return 0;
}

void Box::updateScrollSize()
{
    assert(stackingContext);
    if (clipBox) {
        float s = x + stackingContext->getRelativeX();
        float t = y + stackingContext->getRelativeY();
        // TODO: rtl
        // Use the computed right-margin and bottom-margin values for calculating the block width
        float w = marginLeft + getBorderWidth();
        float h = marginTop + getBorderHeight();
        if (!isAnonymous() && style) {
            if (!style->marginRight.isAuto())
                w += std::max(0.0f, style->marginRight.getPx());
            if (!style->marginBottom.isAuto())
                h += std::max(0.0f, std::min(marginBottom, style->marginBottom.getPx()));  // in case the margin is collapsed
        }
        clipBox->updateScrollWidth(s + w);
        clipBox->updateScrollHeight(t + h);
    }
}

void Box::resetScrollSize()
{
    assert(stackingContext);
    if (clipBox) {
        clipBox->resetScrollWidth();
        clipBox->resetScrollHeight();
    }
}

void Block::updateScrollWidth(float r)
{
    assert(stackingContext);
    float w = r - (x + getBlankLeft() + stackingContext->getRelativeX());
    if (scrollWidth < w)
        scrollWidth = w;
}

void Block::updateScrollHeight(float b)
{
    assert(stackingContext);
    float h = b - (y + getBlankTop() + stackingContext->getRelativeY());
    if (scrollHeight < h)
        scrollHeight = h;
}

void Block::resetScrollWidth()
{
    scrollWidth = 0;
}

void Block::resetScrollHeight()
{
    scrollHeight = 0;
}

const ContainingBlock* Block::getContainingBlock(ViewCSSImp* view) const
{
    if (isAbsolutelyPositioned())
        return &absoluteBlock;
    return Box::getContainingBlock(view);
}

// We also calculate offsetH and offsetV here.
// TODO: Maybe it's better to visit ancestors via the box tree rather than the node tree.
//       cf. CSSContentValueImp::eval()
void Block::setContainingBlock(ViewCSSImp* view)
{
    assert(isAbsolutelyPositioned());
    if (!isFixed()) {
        assert(node);
        for (auto ancestor = node.getParentElement(); ancestor; ancestor = ancestor.getParentElement()) {
            CSSStyleDeclarationImp* style = view->getStyle(ancestor);
            if (!style)
                continue;
            if (style->isPositioned()) {
                // Now we need to find the corresponding box for this ancestor.
                Box* box = style->getBox();
                if (!box)   // cf. html4/tables-001
                    continue;
                offsetH = box->x + box->marginLeft + box->borderLeft - x;
                offsetV = box->y + box->marginTop + box->borderTop - y;
                clipBox = box->clipBox;
                if (Block* block = dynamic_cast<Block*>(box)) {
                    offsetV += block->topBorderEdge;
                    absoluteBlock.width = box->getPaddingWidth();
                    absoluteBlock.height = box->getPaddingHeight();
                    if (style->overflow.isClipped())
                        clipBox = block;
                } else {
                    assert(box->getBoxType() == INLINE_LEVEL_BOX);
                    if (Box* inlineBlock = box->getFirstChild()) {
                        absoluteBlock.width = inlineBlock->getPaddingWidth();
                        absoluteBlock.height = inlineBlock->getPaddingHeight();
                    } else {
                        float t = box->y - box->paddingTop;
                        float l = box->x - box->paddingLeft;
                        box = style->getLastBox();
                        assert(box);
                        float b = box->y + box->height + box->paddingBottom;
                        float r = box->x + box->width + box->paddingRight;
                        absoluteBlock.width = r - l;
                        absoluteBlock.height = b - t;
                    }
                }
                return;
            }
        }
    }
    offsetH = -x;
    offsetV = -y;
    clipBox = 0;
    absoluteBlock.width = view->getInitialContainingBlock()->width;
    absoluteBlock.height = view->getInitialContainingBlock()->height;
}

FormattingContext* Block::updateFormattingContext(FormattingContext* context)
{
    if (isFlowRoot()) {
        assert(formattingContext);
        return formattingContext;
    }
    if (context)
        context->updateBlanks(this);
    return context;
}

FormattingContext* Block::restoreFormattingContext(FormattingContext* context)
{
    if (isFlowRoot())
        return formattingContext;
    else
        context->restoreBlanks(this);
    return context;
}

FormattingContext* Block::establishFormattingContext()
{
    if (!formattingContext);
        formattingContext = new(std::nothrow) FormattingContext;
    return formattingContext;
}

bool Box::isFlowOf(const Block* flowRoot) const
{
    assert(flowRoot->isFlowRoot());
    for (const Box* box = this; box; box = box->getParentBox()) {
        if (box == flowRoot)
            return true;
    }
    return false;
}

float Box::shrinkTo()
{
    return getTotalWidth();
}

void Box::setFlags(unsigned short f)
{
    flags |= f;
    f = 0;
    if (flags & NEED_EXPANSION)
        f |= NEED_CHILD_EXPANSION;
    if (flags & (NEED_REFLOW | NEED_CHILD_REFLOW))
        f |= NEED_CHILD_REFLOW;
    if (!f)
        return;
    for (Box* box = parentBox; box; box = box->parentBox) {
        if ((box->flags & f) == f)
            break;
        if (Block* block = dynamic_cast<Block*>(box)) {
            box->flags |= f;
            if (block->isAnonymous())
                box->flags &= ~NEED_CHILD_EXPANSION;
        }
    }
}

void Box::clearFlags(unsigned short f)
{
    flags &= ~f;
    for (Box* i = firstChild; i; i = i->nextSibling)
        i->clearFlags(f);
}

unsigned short Box::gatherFlags() const
{
    unsigned short f = flags;
    for (auto i = firstChild; i; i = i->nextSibling)
        f |= i->gatherFlags();
    return f;
}

Block::Block(Node node, CSSStyleDeclarationImp* style) :
    Box(node),
    formattingContext(0),
    textAlign(CSSTextAlignValueImp::Default),
    topBorderEdge(0.0f),
    consumed(0.0f),
    inserted(false),
    edge(0.0f),
    remainingHeight(0.0f),
    floatingFirstLetter(0),
    anonymousTable(0),
    defaultBaseline(0.0f),
    defaultLineHeight(0.0f),
    mcw(0.0f),
    scrollWidth(0.0f),
    scrollHeight(0.0f)
{
    if (style)
        setStyle(style);
    flags |= NEED_EXPANSION | NEED_REFLOW | NEED_CHILD_REFLOW;
}

Block::~Block()
{
    delete formattingContext;
    formattingContext = 0;
}

void Block::clearInlines()
{
    setFlags(NEED_EXPANSION | NEED_REFLOW);
}

bool Block::isFloat() const
{
    return !isAnonymous() && style && style->isFloat();
}

bool Block::hasAnonymousBox(Box* prev) const
{
    return prev && prev->isAnonymous();
}

Block* Block::getAnonymousBox(Box* prev)
{
    Block* anonymousBox;
    if (hasAnonymousBox(prev)) {
        anonymousBox = dynamic_cast<Block*>(prev);
        if (anonymousBox)
            return anonymousBox;
    }
    anonymousBox = new(std::nothrow) Block;
    if (anonymousBox) {
        anonymousBox->flags &= ~NEED_EXPANSION;
        anonymousBox->spliceInline(this);
        if (!prev)
            insertBefore(anonymousBox, firstChild);
        else
            insertBefore(anonymousBox, prev->getNextSibling());
        setFlags(anonymousBox->flags);
    }
    return anonymousBox;
}

void Block::resolveBackground(ViewCSSImp* view)
{
    assert(style);
    backgroundColor = style->backgroundColor.getARGB();
    if (style->backgroundImage.isNone()) {
        backgroundImage = 0;
        return;
    }
    DocumentImp* document = dynamic_cast<DocumentImp*>(view->getDocument().self());
    if (backgroundRequest && backgroundRequest->getRequestMessage().getURL() != URL(document->getDocumentURI(), style->backgroundImage.getValue())) {
        delete backgroundRequest;   // TODO: check notifyBackground has been called
        backgroundRequest = 0;
    }
    if (!backgroundRequest) {
        backgroundRequest = new(std::nothrow) HttpRequest(document->getDocumentURI());
        if (backgroundRequest) {
            backgroundRequest->open(u"GET", style->backgroundImage.getValue());
            backgroundRequest->setHandler(boost::bind(&Block::notifyBackground, this, view->getDocument()));
            document->incrementLoadEventDelayCount();
            retain_();
            backgroundRequest->send();
        }
    }
    if (backgroundRequest)
        backgroundImage = backgroundRequest->getBoxImage(style->backgroundRepeat.getValue());
}

void Block::notifyBackground(Document document)
{
    if (backgroundRequest->getStatus() == 200)
        setFlags(NEED_REFLOW);
    if (auto* imp = dynamic_cast<DocumentImp*>(document.self()))
        imp->decrementLoadEventDelayCount();
    release_();
}

void Block::resolveBackgroundPosition(ViewCSSImp* view, const ContainingBlock* containingBlock)
{
    assert(style);
    if (!backgroundImage || backgroundImage->getState() != BoxImage::CompletelyAvailable)
        return;
    if (getParentBox() || !style->backgroundAttachment.isFixed())
        style->backgroundPosition.resolve(view, backgroundImage, style.get(), getPaddingWidth(), getPaddingHeight());
    else
        style->backgroundPosition.resolve(view, backgroundImage, style.get(), containingBlock->width, containingBlock->height);
    backgroundLeft = style->backgroundPosition.getLeftPx();
    backgroundTop = style->backgroundPosition.getTopPx();
}

float Block::setWidth(float w, float maxWidth, float minWidth)
{
    float d = 0.0f;
    if (maxWidth < w) {
        d = w - maxWidth;
        w = maxWidth;
        // cf. html4/max-width-applies-to-007.htm (note that strictly speaking, this test is invalid.)
        if (!style->width.isAuto() && !style->width.isPercentage())
            updateMCW(std::min(style->width.getPx(), maxWidth));
    } else if (!style->width.isAuto() && !style->width.isPercentage())
        updateMCW(style->width.getPx());
    if (w < minWidth) {
        d -= (minWidth - w);
        w = minWidth;
        updateMCW(minWidth);
    }
    width = w;
    return d;
}

// Calculate width
//
// marginLeft + borderLeftWidth + paddingLeft + width + paddingRight + borderRightWidth + marginRight
// == containingBlock->width (- scrollbar width, if any)
float Block::resolveWidth(float w, FormattingContext* context, float r)
{
    if (isAnonymous()) {
        width = w;
        return width;
    }

    float maxWidth = HUGE_VALF;
    float minWidth = 0.0f;
    int autoCount = 3;
    unsigned autoMask = Left | Width | Right;
    if (!style)
        width = 0.0f;
    else {
        maxWidth = style->maxWidth.isNone() ? HUGE_VALF : style->maxWidth.getPx();
        minWidth = style->minWidth.getPx();
        if (style->isFloat() || style->display.isInlineLevel()) {
            resolveFloatWidth(w, maxWidth, minWidth);
            return width;
        }
        if (intrinsic) {
            --autoCount;
            autoMask &= ~Width;
            w -= width;
        } else if (!isnan(r)) {
            width = r;
            --autoCount;
            autoMask &= ~Width;
            w -= width;
        } else if (!style->width.isAuto()) {
            width = style->width.getPx();
            --autoCount;
            autoMask &= ~Width;
            w -= width;
        } else
            width = 0.0f;
        if (!style->marginLeft.isAuto()) {
            marginLeft = style->marginLeft.getPx();
            --autoCount;
            autoMask &= ~Left;
            w -= marginLeft;
        }
        if (!style->marginRight.isAuto()) {
            marginRight = style->marginRight.getPx();
            --autoCount;
            autoMask &= ~Right;
            w -= marginRight;
        }
    }
    w -= borderLeft + paddingLeft + paddingRight + borderRight;
    if (w < 0.0f && !(autoMask & Width))
        w = 0.0f;

    float leftover = w + width;
    float ld = 0.0f;
    float rd = 0.0f;
    if (isFlowRoot() && context) {
        float e;
        if (0.0f < (e = context->getLeftEdge()))
            ld = std::max(0.0f, e - marginLeft);
        if (0.0f < (e = context->getRightEdge()))
            rd = std::max(0.0f, e - marginRight);
        w -= ld + rd;
        leftover = w + width;
        if (w < 0.0f && !(autoMask & Width))
            w = 0.0f;
    }

    float d;
    switch (autoMask) {
    case Left | Width | Right:
        d = setWidth(w, maxWidth, minWidth);
        marginLeft = ld + d / 2.0f;
        marginRight = rd + d / 2.0f;
        break;
    case Left | Width:
        d = setWidth(w, maxWidth, minWidth);
        marginLeft = ld;
        marginRight += rd + d;
        break;
    case Width | Right:
        d = setWidth(w, maxWidth, minWidth);
        marginLeft += ld + d;
        marginRight = rd;
        break;
    case Left | Right:
        w += setWidth(width, maxWidth, minWidth);
        marginLeft = w / 2.0f + ld;
        marginRight = w / 2.0f + rd;
        break;
    case Left:
        w += setWidth(width, maxWidth, minWidth);
        marginLeft = w + ld;
        marginRight += rd;
        break;
    case Width:
        d = setWidth(w, maxWidth, minWidth);
        marginLeft += ld;
        marginRight += rd + d;  // TODO: assuming LTR
        break;
    case Right:
        w += setWidth(width, maxWidth, minWidth);
        marginLeft += ld;
        marginRight = w + rd;
        break;
    default:  // over-constrained
        w += setWidth(width, maxWidth, minWidth);
        marginLeft += ld;
        marginRight += w + rd;   // TODO: assuming LTR
        break;
    }

    return leftover;
}

void Block::resolveFloatWidth(float w, float maxWidth, float minWidth)
{
    assert(style);
    marginLeft = style->marginLeft.isAuto() ? 0.0f : style->marginLeft.getPx();
    marginRight = style->marginRight.isAuto() ? 0.0f : style->marginRight.getPx();
    if (!style->width.isAuto())
        setWidth(style->width.getPx(), maxWidth, minWidth);
    else
        setWidth(w - getBlankLeft() - getBlankRight(), maxWidth, minWidth);
}

void Block::resolveHeight()
{
    if (!style->marginTop.isAuto())
        marginTop = style->marginTop.getPx();
    else
        marginTop = 0.0f;
    if (!style->marginBottom.isAuto())
        marginBottom = style->marginBottom.getPx();
    else
        marginBottom = 0.0f;
    if (!style->height.isAuto())
        height = style->height.getPx();
    else
        height = 0.0f;
}

void Block::layOutInlineBlock(ViewCSSImp* view, Node node, Block* inlineBlock, FormattingContext* context)
{
    assert(inlineBlock->style);

    InlineBox* inlineBox = new(std::nothrow) InlineBox(node, inlineBlock->style.get());
    if (!inlineBox)
        return;  // TODO error

    if (!context->lineBox) {
        if (!context->addLineBox(view, this))
            return;  // TODO error
    }

    context->prevChar = 0;
    inlineBox->parentBox = context->lineBox;  // for getContainingBlock
    inlineBox->appendChild(inlineBlock);
    inlineBox->width = inlineBlock->getTotalWidth();
    inlineBox->height = inlineBlock->getTotalHeight();
    if (inlineBox->height == 0.0f)
        inlineBox->width = 0.0f;
    inlineBox->baseline = inlineBox->height;
    if (!inlineBlock->style->overflow.isClipped()) {
        if (TableWrapperBox* table = dynamic_cast<TableWrapperBox*>(inlineBlock))
            inlineBox->baseline = table->getBaseline();
        else
            inlineBox->baseline = inlineBlock->getBaseline();
    }
    while (context->leftover < inlineBox->getTotalWidth() &&
           (context->breakable || (style && style->whiteSpace.isBreakingLines())))
    {
        if (context->lineBox->hasChildBoxes() || context->hasNewFloats()) {
            context->nextLine(view, this, false);
            if (!context->addLineBox(view, this))
                return;  // TODO error
            continue;
        }
        if (!context->shiftDownLineBox(view))
            break;
    }

    context->x += inlineBox->getTotalWidth();
    context->leftover -= inlineBox->getTotalWidth();
    context->appendInlineBox(view, inlineBox, inlineBlock->style.get());

    updateMCW(inlineBox->getTotalWidth());
}

void Block::layOutFloat(ViewCSSImp* view, Node node, Block* floatingBox, FormattingContext* context)
{
    assert(floatingBox->style);
    floatingBox->remainingHeight = floatingBox->getTotalHeight();
    if (!context->floatingBoxes.empty()) {
        // Floats are not allowed to reorder. Process this floating box later in the other line box.
        context->floatingBoxes.push_back(floatingBox);
        return;
    }
    unsigned clear = floatingBox->style->clear.getValue();
    if ((clear & CSSClearValueImp::Left) && context->getLeftEdge() ||
        (clear & CSSClearValueImp::Right) && context->getRightEdge()) {
        context->floatingBoxes.push_back(floatingBox);
        return;
    }
    if (!context->lineBox) {
        if (!context->addLineBox(view, this))
            return;   // TODO error
    }
    float w = floatingBox->getEffectiveTotalWidth();
    float l = context->getLeftoverForFloat(this, floatingBox->style->float_.getValue());
    // If both w and l are zero, move this floating box to the next line;
    // cf. http://test.csswg.org/suites/css2.1/20110323/html4/stack-floats-003.htm
    if ((l < w || l == 0.0f && w == 0.0f) &&
        (context->lineBox->hasChildBoxes() || context->hasLeft() || context->hasRight())) {
        // Process this float box later in the other line box.
        context->floatingBoxes.push_back(floatingBox);
        return;
    }
    context->addFloat(floatingBox, w);
}

void Block::layOutAbsolute(ViewCSSImp* view, Node node, Block* absBox, FormattingContext* context)
{
    // Just insert this absolute box into a line box now.
    // Absolute boxes will be processed later in ViewCSSImp::layOut().
    if (!context->lineBox) {
        if (!context->addLineBox(view, this))
            return;  // TODO error
    }
    context->lineBox->appendChild(absBox);
}

// Generate line boxes
bool Block::layOutInline(ViewCSSImp* view, FormattingContext* context, float originalMargin)
{
    if (!(flags & NEED_REFLOW))
        return true;

    bool keepConsumed = false;
    marginUsed = false;
    context->atLineHead = true;

    removeChildren();

    bool collapsed = true;
    for (auto i = inlines.begin(); i != inlines.end(); ++i) {
        Node node = *i;
        Block* block = findBlock(node);
        if (block && block != this) {  // Check an empty absolutely positioned box; cf. bottom-applies-to-010.
            block->parentBox = this;
            context->useMargin(this);
            if (block->isFloat()) {
                if (block->style->clear.getValue())
                    keepConsumed = true;
                layOutFloat(view, node, block, context);
            } else if (block->isAbsolutelyPositioned())
                layOutAbsolute(view, node, block, context);
            else
                layOutInlineBlock(view, node, block, context);
            collapsed = false;
        } else {
            CSSStyleDeclarationImp* style = 0;
            Element element = getContainingElement(node);
            if (!element)
                continue;
            style = view->getStyle(element);
            if (!style)
                continue;
            if (style->display.isInline())
                style->resolve(view, this);
            if (node.getNodeType() == Node::TEXT_NODE) {
                Text text = interface_cast<Text>(node);
                if (layOutText(view, node, context, text.getData(), element, style))
                    collapsed = false;
            } else {
                // empty inline element
                if (layOutText(view, node, context, u"", element, style))
                    collapsed = false;
            }
        }
    }
    if (context->lineBox)
        context->nextLine(view, this, false);

    // Layout remaining floating boxes in context
    while (!context->floatingBoxes.empty()) {
        Block* floatingBox = context->floatingBoxes.front();
        float clearance = 0.0f;
        if (unsigned clear = floatingBox->style->clear.getValue()) {
            keepConsumed = true;
            clearance = -context->usedMargin;
            clearance += context->clear(clear);
            // Note if no clearance is added, we do shift down at least one
            // floating box; cf. testdata/css-007.html
        }
        if (clearance <= 0.0f) {
            context->leftover = width - context->getLeftEdge() - context->getRightEdge();
            while (context->getLeftoverForFloat(this, floatingBox->style->float_.getValue()) < floatingBox->getEffectiveTotalWidth()) {
                float h = context->shiftDown();
                if (h <= 0.0f)
                    break;
                clearance += h;
                context->clearance += h;
                context->adjustRemainingHeight(clearance);
            }
        }
        LineBox* nextLine = context->addLineBox(view, this);
        context->nextLine(view, this, false);
        if (nextLine && clearance != 0.0f)
            nextLine->clearance = clearance;
    }
    if (!keepConsumed)
        consumed = 0.0f;
    if (collapsed && isAnonymous()) {
        undoCollapseMarginTop(context, originalMargin);
        return false;
    }
    return true;
}

// TODO for a more complete implementation, see,
//      http://groups.google.com/group/netscape.public.mozilla.layout/msg/0455a21b048ffac3?pli=1

void Block::shrinkToFit(FormattingContext* context)
{
    fit(shrinkTo(), context);
}

float Block::shrinkTo()
{
    int autoCount = 3;
    float min = 0.0f;
    if (style && !style->width.isAuto()) {
        --autoCount;
        min = style->width.getPx();
    } else {
        for (Box* child = getFirstChild(); child; child = child->getNextSibling())
            min = std::max(min, child->shrinkTo());
    }
    min += borderLeft + paddingLeft + paddingRight + borderRight;
    if (style) {
        if (!style->marginLeft.isAuto()) {
            --autoCount;
            min += style->marginLeft.getPx();
        }
        if (!style->marginRight.isAuto()) {
            --autoCount;
            float m  = style->marginRight.getPx();
            if (0.0f < m)
                min += m;
        }
    }
    return min;
}

void Block::fit(float w, FormattingContext* context)
{
    if (getBlockWidth() == w)
        return;
    resolveWidth(w, context);
    if (!isAnonymous() && !style->width.isAuto())
        return;
    for (Box* child = getFirstChild(); child; child = child->getNextSibling())
        child->fit(width, context);
}

float Block::getBaseline(const Box* box) const
{
    float baseline = NAN;
    float h = box->getBlankTop();
    for (Box* i = box->getFirstChild(); i; i = i->getNextSibling()) {
        if (TableWrapperBox* table = dynamic_cast<TableWrapperBox*>(i))
            baseline = h + table->getBaseline() + table->offsetV;
        else if (Block* block = dynamic_cast<Block*>(i)) {
            float x = getBaseline(block);
            if (!isnan(x))
                baseline = h + x + block->offsetV;
        } else if (LineBox* lineBox = dynamic_cast<LineBox*>(i)) {
            if (lineBox->hasInlineBox())
                baseline = h + lineBox->getBaseline();
        }
        h += i->getTotalHeight();
        if (box->height != 0.0f || !dynamic_cast<LineBox*>(box->getFirstChild()))
            h += i->getClearance();
    }
    return baseline;
}

// TODO: We should calculate the baseline once and just return it.
float Block::getBaseline() const
{
    float x = getBaseline(this);
    return isnan(x) ? getTotalHeight() : x;
}

bool Block::isCollapsableInside() const
{
    return !isFlowRoot();
}

bool Block::isCollapsableOutside() const
{
    if (!isInFlow())
        return false;
    if (!isAnonymous() && style) {
        if (style->display.isInlineLevel() || style->display.getValue() == CSSDisplayValueImp::TableCell)
            return false;
    }
    return true;
}

bool Block::isCollapsedThrough() const
{
    if (height != 0.0f || isFlowRoot() ||
        borderTop != 0.0f || paddingTop != 0.0f || paddingBottom != 0.0f || borderBottom != 0.0f)
        return false;
    for (LineBox* lineBox = dynamic_cast<LineBox*>(getFirstChild());
         lineBox;
         lineBox = dynamic_cast<LineBox*>(lineBox->getNextSibling())) {
        if (lineBox->getTotalHeight() != 0.0f)
            return false;
        for (auto i = lineBox->getFirstChild(); i; i = i->getNextSibling()) {
            if (dynamic_cast<InlineBox*>(i))
                return false;
        }
    }
    return true;
}

float Block::getInternalClearances() const
{
    if (height != 0.0f || isFlowRoot() ||
        borderTop != 0.0f || paddingTop != 0.0f || paddingBottom != 0.0f || borderBottom != 0.0f)
        return 0.0f;
    float c = 0.0f;
    for (LineBox* lineBox = dynamic_cast<LineBox*>(getFirstChild());
         lineBox;
         lineBox = dynamic_cast<LineBox*>(lineBox->getNextSibling()))
    {
        if (lineBox->getTotalHeight() != 0.0f)
            return 0.0f;
        if (!isnan(lineBox->clearance))
            c += lineBox->clearance;
    }
    if (c != 0.0f)
        c += topBorderEdge;
    return c;
}

float Block::collapseMarginTop(FormattingContext* context)
{
    if (!isCollapsableOutside()) {
        assert(!isAnonymous());
        if (isFloat() || isAbsolutelyPositioned() || !getParentBox())
            return NAN;
        assert(context);
        Block* prev = dynamic_cast<Block*>(getPreviousSibling());
        if (prev && prev->isCollapsableOutside())
            context->collapseMargins(prev->marginBottom);
        context->fixMargin();

        float clearance = context->clear(style->clear.getValue());
        if (clearance == 0.0f)
            clearance = NAN;
        else {
            if (clearance < marginTop)
                clearance = marginTop;
            clearance -= marginTop;
            context->collapseMargins(marginTop);
            context->setClearance();
        }
        return NAN;
    }

    float original = marginTop;
    float before = NAN;
    if (Block* parent = dynamic_cast<Block*>(getParentBox())) {
        if (parent->getFirstChild() == this) {
            if (parent->isCollapsableInside() && parent->borderTop == 0 && parent->paddingTop == 0 && !hasClearance()) {
                before = parent->marginTop;
                parent->marginTop = 0.0f;
            }
        } else {
            Block* prev = dynamic_cast<Block*>(getPreviousSibling());
            if (prev && prev->isCollapsableOutside()) {
                before = context->collapseMargins(prev->marginBottom);
                prev->marginBottom = 0.0f;
                if (prev->isCollapsedThrough())
                    prev->marginTop = 0.0f;
            }
        }
    }
    marginTop = context->collapseMargins(marginTop);

    if (!isAnonymous()) {
        unsigned clear = style->clear.getValue();
        clearance = context->clear(clear);
        Block* prev = dynamic_cast<Block*>(getPreviousSibling());
        if (clear && prev)
            clearance += prev->getInternalClearances();
        if (clearance == 0.0f)
            clearance = NAN;
        else if (prev && prev->isCollapsedThrough()) {
            if (clearance < marginTop)
                clearance = marginTop;
            prev->marginBottom = before;
            clearance -= original + before;
            marginTop = original;
            before = NAN;
            context->collapseMargins(marginTop);
            context->setClearance();
        } else if (clearance < marginTop) {
            clearance = NAN;
            before = NAN;
            context->collapseMargins(marginTop);
        } else {
            if (prev) {
                prev->marginBottom = context->undoCollapseMargins();
                clearance -= original + before;
            } else
                clearance -= original;
            marginTop = original;
            before = NAN;
            context->collapseMargins(marginTop);
            context->setClearance();
        }
    }

    if (0.0f < context->clearance) {
        if (isnan(clearance))
            clearance = context->clearance;
        else
            clearance += context->clearance;
        context->setClearance();
    }

    return before;
}

void Block::collapseMarginBottom(FormattingContext* context)
{
    float used = 0.0f;

    Block* last = dynamic_cast<Block*>(getLastChild());
    if (last && last->isCollapsableOutside()) {
        float lm = context->collapseMargins(last->marginBottom);
        if (last->isCollapsedThrough()) {
            lm = context->collapseMargins(last->marginTop);
            last->marginTop = 0.0f;
            if (isCollapsableInside() && borderBottom == 0 && paddingBottom == 0 && style->height.isAuto() &&
                !context->hasClearance())
            {
                last->marginBottom = 0.0f;
                marginBottom = context->collapseMargins(marginBottom);
            } else {
                last->marginBottom = lm;
                // Save the consumed margin which is to be used as the bottom clearance
                // of a flow root parent box; cf. margin-collapse-145.
                used = context->fixMargin();
                if (!last->hasClearance())
                    last->moveUpCollapsedThroughMargins(context);
            }
        } else if (isCollapsableInside() && borderBottom == 0 && paddingBottom == 0 && style->height.isAuto()) {
            last->marginBottom = 0.0f;
            marginBottom = context->collapseMargins(marginBottom);
        } else {
            last->marginBottom = lm;
            context->fixMargin();
        }
    }

    Block* first = dynamic_cast<Block*>(getFirstChild());
    if (first && first->isCollapsableOutside() && !first->hasClearance() && isCollapsableInside() && borderTop == 0 && paddingTop == 0) {
        if (hasClearance()) {
            // The following algorithm is deduced from the following tests:
            //   http://test.csswg.org/suites/css2.1/20110323/html4/margin-collapse-157.htm
            //   http://test.csswg.org/suites/css2.1/20110323/html4/margin-collapse-clear-015.htm
            //   http://hixie.ch/tests/evil/acid/002-no-data/#top (clearance < 0.0f)
            float original = style->marginTop.isAuto() ? 0 : style->marginTop.getPx();
            if (clearance <= 0.0f)
                marginTop = std::max(original, first->marginTop);
            else if (original < first->marginTop - clearance)
                marginTop = first->marginTop - clearance;
            else
                marginTop = original;
            first->marginTop = 0.0f;
        } else {
            // Note even if first->marginTop is zero, first->topBorderEdge
            // still needs to be cleared; cf. margin-bottom-103.
            std::swap(first->marginTop, marginTop);
            while (first && first->isCollapsedThrough()) {
                // The top border edge must not be cleared if the next adjacent sibling has a clearance;
                // cf. clear-001.
                Block* next = dynamic_cast<Block*>(first->getNextSibling());
                if (!next || (!next->hasClearance() && next->consumed <= 0.0f))
                    first->topBorderEdge = 0.0f;
                else
                    break;
                first = next;
            }
        }
    }

    if (isFlowRoot() && getLastChild()) {
        // Keep the consumed height by the last collapsed through box in marginBottom.
        // cf. margin-collapse-145.
        getLastChild()->marginBottom += std::max(used, context->clear(3));
    }
}

bool Block::undoCollapseMarginTop(FormattingContext* context, float before)
{
    if (isnan(before))
        return false;
    if (Block* prev = dynamic_cast<Block*>(getPreviousSibling()))
        prev->marginBottom = context->undoCollapseMargins();
    else {
        Box* parent = getParentBox();
        assert(parent);
        parent->marginTop = context->undoCollapseMargins();
    }
    return true;
}

// Adjust marginTop of the 1st, collapsed through child box.
void Block::adjustCollapsedThroughMargins(FormattingContext* context)
{
    if (isCollapsedThrough()) {
        topBorderEdge = marginTop;
        if (hasClearance())
            moveUpCollapsedThroughMargins(context);
    } else if (isCollapsableOutside()) {
        assert(topBorderEdge == 0.0f);
        context->fixMargin();
        moveUpCollapsedThroughMargins(context);
    }
}

void Block::moveUpCollapsedThroughMargins(FormattingContext* context)
{
    assert(isCollapsableOutside());
    float m;
    Block* from = this;
    Block* curr = this;
    Block* prev = dynamic_cast<Block*>(curr->getPreviousSibling());
    if (hasClearance()) {
        if (!prev)
            return;
        from = curr = prev;
        prev = dynamic_cast<Block*>(curr->getPreviousSibling());
        if (from->hasClearance() || !from->isCollapsedThrough())
            return;
        m = curr->marginTop;
    } else if (curr->isCollapsedThrough()) {
        assert(curr->marginTop == 0.0f);
        // cf. If previously a part of marginTop has been used for consuming some
        // floating box heights, leave it as marginTop; cf. clear-float-003.
        curr->marginTop = consumed;
        m = curr->marginBottom - consumed;
        curr->topBorderEdge = 0.0f;
        for (Block* last = dynamic_cast<Block*>(curr->getLastChild());
             last && last->isCollapsedThrough();
             last = dynamic_cast<Block*>(last->getPreviousSibling())) {
            last->topBorderEdge = 0.0f;
            if (last->hasClearance())
                break;
        }
    } else
        m = curr->marginTop - consumed;
    while (prev && prev->isCollapsedThrough() && !prev->hasClearance()) {
        prev->topBorderEdge -= m;
        curr = prev;
        prev = dynamic_cast<Block*>(curr->getPreviousSibling());
    }
    if (curr != from) {
        assert(curr->marginTop == 0.0f);
        assert(curr->marginBottom == 0.0f);
        curr->marginTop = m;
        if (!from->isCollapsedThrough() || hasClearance())
            from->marginTop -= m;
        else
            from->marginBottom = 0.0f;
    } else if (curr->isCollapsedThrough() && !hasClearance()) {
        curr->marginTop += m;
        curr->marginBottom = 0.0f;
        curr->topBorderEdge = 0.0f;
    }
}

void Block::layOutChildren(ViewCSSImp* view, FormattingContext* context)
{
    Box* next;
    for (Box* child = getFirstChild(); child; child = next) {
        next = child->getNextSibling();
        if (!child->layOut(view, context)) {
            removeChild(child);
            child->release_();
        } else
            updateMCW(child->getMCW());
    }
}

void Block::applyMinMaxHeight(FormattingContext* context)
{
    assert(!isAnonymous());
    if (!style->maxHeight.isNone()) {
        float maxHeight = style->maxHeight.getPx();
        if (maxHeight < height)
            height = maxHeight;
    }
    if (!hasChildBoxes() && 0.0f < height)
        context->updateRemainingHeight(height);
    float min = style->minHeight.getPx();
    float d = min - height;
    if (0.0f < d) {
        context->updateRemainingHeight(d);
        height = min;
    }
}

void Block::layOutInlineBlocks(ViewCSSImp* view)
{
    for (auto i = blockMap.begin(); i != blockMap.end(); ++i) {
        Block* block = i->second.get();
        if (!block->isAbsolutelyPositioned()) {
            float savedWidth = block->getTotalWidth();
            float savedHeight = block->getTotalHeight();
            // TODO: Check block's baseline as well.
            block->layOut(view, 0);
            if (block->isAnonymous() || (dynamic_cast<TableWrapperBox*>(block) &&
                                         (block->style->display != CSSDisplayValueImp::Table &&
                                          block->style->display != CSSDisplayValueImp::InlineTable)))
            {
                block->marginLeft = 0.0f;
                block->marginRight = 0.0f;
            } else {
                if (block->style->marginLeft.isAuto())
                    block->marginLeft = 0.0f;
                if (block->style->marginRight.isAuto())
                    block->marginRight = 0.0f;
            }
            if (savedWidth != block->getTotalWidth() || savedHeight != block->getTotalHeight()) {
                flags |= NEED_REFLOW;
                // Set NEED_REFLOW to anonymous boxes between 'block' and 'this'.
                for (Box* ancestor = block->parentBox; ancestor && ancestor != this; ancestor = ancestor->parentBox) {
                    if (dynamic_cast<Block*>(ancestor))
                        ancestor->flags |= NEED_REFLOW;
                }
            }
        }
    }
}

bool Block::layOut(ViewCSSImp* view, FormattingContext* context)
{
    const ContainingBlock* containingBlock = getContainingBlock(view);

    Element element = 0;
    if (!isAnonymous())
        element = getContainingElement(node);
    else if (const Box* box = dynamic_cast<const Box*>(containingBlock)) {
        do {
            if (box->node) {
                element = getContainingElement(box->node);
                if (element)
                    break;
            }
        } while (box = box->getParentBox());
    }
    if (!element)
        return false;  // TODO error

#ifndef NDEBUG
    std::u16string tag = interface_cast<html::HTMLElement>(element).getTagName();
#endif

    style = view->getStyle(element);
    if (!style)
        return false;  // TODO error

    float savedWidth = width;
    float savedHeight = height;
    float savedMcw = mcw;
    float leftover = 0.0f;

    mcw = 0.0f;
    if (!isAnonymous()) {
        style->addBox(this);
        flags |= style->resolve(view, containingBlock);
        resolveBackground(view);
        updatePadding();
        updateBorderWidth();
        resolveHeight();
        leftover = resolveWidth(containingBlock->width, context);
    } else {
        // The properties of anonymous boxes are inherited from the enclosing non-anonymous box.
        // Theoretically, we are supposed to create a new style for this anonymous box, but
        // of course we don't want to do so.
        backgroundColor = 0x00000000;
        paddingTop = paddingRight = paddingBottom = paddingLeft = 0.0f;
        borderTop = borderRight = borderBottom = borderLeft = 0.0f;
        marginTop = marginRight = marginLeft = marginBottom = 0.0f;
        width = containingBlock->width;
        height = 0.0f;
        stackingContext = style->getStackingContext();
    }

    float cw(NAN);
    CellBox* cell = dynamic_cast<CellBox*>(this);
    if (cell)
        cw = cell->adjustWidth();

     if (savedWidth != width)
         flags |= NEED_REFLOW;

    visibility = style->visibility.getValue();
    textAlign = style->textAlign.getValue();

    float before = NAN;
    if (context) {
        before = collapseMarginTop(context);
        if (!isnan(context->clearance))
            leftover = resolveWidth(containingBlock->width, context);
        if (isInFlow() && 0.0f < borderTop + paddingTop)
            context->updateRemainingHeight(borderTop + paddingTop);
        if (!needLayout()) {
#ifndef NDEBUG
            if (3 <= getLogLevel())
                std::cout << "Block::" << __func__ << ": skip reflow for '" << tag << "'\n";
#endif
            height = savedHeight;
            mcw = savedMcw;
            context->restoreContext(this);
            return true;
        }
    }
    FormattingContext* parentContext = context;
    context = updateFormattingContext(context);

    layOutInlineBlocks(view);
    if (hasInline() && !(flags & NEED_REFLOW)) {
        // Only inline blocks have been marked NEED_REFLOW and no more reflow is necessary
        // with this block.
#ifndef NDEBUG
        if (3 <= getLogLevel())
            std::cout << "Block::" << __func__ << ": skip reflow for '" << tag << "'\n";
#endif
        flags &= ~(NEED_CHILD_REFLOW | NEED_REPOSITION);
        restoreFormattingContext(context);
        if (parentContext && context != parentContext)
            context = parentContext;
        height = savedHeight;
        mcw = savedMcw;
        if (context) {
            context->restoreContext(this);
            return true;
        }
    }

    CSSAutoLengthValueImp originalWidth = style->width;
    CSSAutoLengthValueImp originalHeight = style->height;
    if (!layOutReplacedElement(view, element, style.get())) {
        if (!intrinsic && style->display.isInline() && isReplacedElement(element)) {
            // An object fallback has occurred for an inline, replaced element.
            // It is now treated as an inline element, and hence 'width' and 'height'
            // are not applicable.
            // cf. http://www.webstandards.org/action/acid2/guide/#row-4-5
            style->width.setValue();
            style->height.setValue();
        }
        if (hasInline()) {
            if (!layOutInline(view, context, before)) {
                style->width = originalWidth;
                style->height = originalHeight;
                return false;
            }
        }
    }

    if (flags & NEED_REFLOW) {
        if (getFirstChild() && dynamic_cast<Block*>(getFirstChild())) {
            // Note to avoid NEED_CHILD_REFLOW flags are set back again
            // to the ancestor boxes, do not use setFlags() here:
            getFirstChild()->flags |= NEED_REFLOW;
        }
    }
    layOutChildren(view, context);
    if (!isAnonymous()) {
        if ((style->width.isAuto() || style->marginLeft.isAuto() || style->marginRight.isAuto()) &&
            (style->isInlineBlock() || style->isFloat() || (cell && isnan(cw)) || isReplacedElement(element)) &&
            !intrinsic)
            shrinkToFit(parentContext);

        if (!cell) {
            mcw += borderLeft + borderRight;
            if (!style->paddingLeft.isPercentage())
                mcw += style->paddingLeft.getPx();
            if (!style->paddingRight.isPercentage())
                mcw += style->paddingRight.getPx();
            if (!style->marginLeft.isPercentage() && !style->marginLeft.isAuto())
                mcw += style->marginLeft.getPx();
            if (!style->marginRight.isPercentage() && !style->marginRight.isAuto())
                mcw += style->marginRight.getPx();
        } else if (isnan(cw))
            mcw += getBlankLeft() + getBlankRight();
    } else if (cell)
        shrinkToFit(parentContext);

    // Collapse margins with the first and the last children before calculating the auto height.
    collapseMarginBottom(context);

    // Note the table cell's 'height' property does not increase the height of the cell box.
    // cf. http://www.w3.org/TR/CSS21/tables.html#height-layout
    if ((style->height.isAuto() && !intrinsic) || isAnonymous() || cell) {
        float totalClearance = 0.0f;
        height = 0.0f;
        for (Box* child = getFirstChild(); child; child = child->getNextSibling()) {
            height += child->getTotalHeight();
            totalClearance += child->getClearance();
        }
        // If height is zero and this block-box contains only line-boxes,
        // clearances are used just to layout floating boxes, and thus
        // totalClearance should not be added to height.
        // TODO: test more conditions.
        if (height != 0.0f || !dynamic_cast<LineBox*>(getFirstChild()))
            height += totalClearance;
    }
    if (!isAnonymous()) {
        applyMinMaxHeight(context);
        // TODO: If min-height was applied, we might need to undo collapseMarginBottom().
    } else if (!hasChildBoxes() && 0.0f < height)
        context->updateRemainingHeight(height);

    // Now that 'height' is fixed, calculate 'left', 'right', 'top', and 'bottom'.
    for (Box* child = getFirstChild(); child; child = child->getNextSibling())
        child->fit(width, parentContext);

    resolveBackgroundPosition(view, containingBlock);

    restoreFormattingContext(context);
    if (parentContext && parentContext != context) {
        if (isCollapsableOutside()) {
            // TODO: Review this logic; what's going to happen when collapse through, etc.
            // Note TableWrapperBox::layOut() has the same code and it must be updated, too.
            parentContext->inheritMarginContext(context);
            if (!isAnonymous() && isFlowRoot() && leftover < width) {
                float h;
                while (0 < (h = parentContext->shiftDown(&leftover))) {
                    parentContext->updateRemainingHeight(h);
                    if (isnan(clearance))
                        clearance = h;
                    else
                        clearance += h;
                    if (width <= leftover)
                        break;
                }
                if (!isnan(clearance)) {
                    // TODO: Adjust clearance and set margins to the original values
                    resolveWidth(containingBlock->width, parentContext, width);
                    updateFormattingContext(parentContext);
                }
            }
        }
        context = parentContext;
        if (0.0f < height)
            context->updateRemainingHeight(height);
    }


    if (context->hasChanged(this)) {
        context->saveContext(this);
        if (nextSibling)
            nextSibling->flags |= NEED_REFLOW;
    }
    flags &= ~(NEED_REFLOW | NEED_CHILD_REFLOW | NEED_REPOSITION);

    adjustCollapsedThroughMargins(context);
    if (isInFlow() && 0.0f < paddingBottom + borderBottom)
        context->updateRemainingHeight(paddingBottom + borderBottom);

    style->width = originalWidth;
    style->height = originalHeight;
    return true;
}

unsigned Block::resolveAbsoluteWidth(const ContainingBlock* containingBlock, float& left, float& right, float r)
{
    //
    // Calculate width
    //
    // left + marginLeft + borderLeftWidth + paddingLeft + width + paddingRight + borderRightWidth + marginRight + right
    // == containingBlock->width
    //
    marginLeft = style->marginLeft.isAuto() ? 0.0f : style->marginLeft.getPx();
    marginRight = style->marginRight.isAuto() ? 0.0f : style->marginRight.getPx();

    left = 0.0f;
    right = 0.0f;

    unsigned autoMask = Left | Width | Right;
    if (!style->left.isAuto()) {
        left = style->left.getPx();
        autoMask &= ~Left;
    }
    if (!isnan(r)) {
        width = r;
        autoMask &= ~Width;
    } else if (!style->width.isAuto()) {
        width = style->width.getPx();
        autoMask &= ~Width;
    }
    if (!style->right.isAuto()) {
        right = style->right.getPx();
        autoMask &= ~Right;
    }
    float leftover = containingBlock->width - getTotalWidth() - left - right;
    switch (autoMask) {
    case Left | Width | Right:
        left = -offsetH;
        autoMask &= ~Left;
        // FALL THROUGH
    case Width | Right:
        width += leftover;  // Set the max size and do shrink-to-fit later.
        break;
    case Left | Width:
        width += leftover;  // Set the max size and do shrink-to-fit later.
        break;
    case Left | Right:
        left = -offsetH;
        right += leftover - left;
        break;
    case Left:
        left += leftover;
        break;
    case Width:
        width += leftover;
        break;
    case Right:
        right += leftover;
        break;
    case 0:
        if (style->marginLeft.isAuto() && style->marginRight.isAuto()) {
            if (0.0f <= leftover)
                marginLeft = marginRight = leftover / 2.0f;
            else {  // TODO rtl
                marginLeft = 0.0f;
                marginRight = -leftover;
            }
        } else if (style->marginLeft.isAuto())
            marginLeft = leftover;
        else if (style->marginRight.isAuto())
            marginRight = leftover;
        else
            right += leftover;
        break;
    }
    return autoMask;
}

unsigned Block::applyAbsoluteMinMaxWidth(const ContainingBlock* containingBlock, float& left, float& right, unsigned autoMask)
{
    if (!style->maxWidth.isNone()) {
        float maxWidth = style->maxWidth.getPx();
        if (maxWidth < width)
            autoMask = resolveAbsoluteWidth(containingBlock, left, right, maxWidth);
    }
    float minWidth = style->minWidth.getPx();
    if (width < minWidth)
        autoMask = resolveAbsoluteWidth(containingBlock, left, right, minWidth);
    return autoMask;
}

unsigned Block::resolveAbsoluteHeight(const ContainingBlock* containingBlock, float& top, float& bottom, float r)
{
    //
    // Calculate height
    //
    // top + marginTop + borderTopWidth + paddingTop + height + paddingBottom + borderBottomWidth + marginBottom + bottom
    // == containingBlock->height
    //
    marginTop = style->marginTop.isAuto() ? 0.0f : style->marginTop.getPx();
    marginBottom = style->marginBottom.isAuto() ? 0.0f : style->marginBottom.getPx();

    top = 0.0f;
    bottom = 0.0f;

    unsigned autoMask = Top | Height | Bottom;
    if (!style->top.isAuto()) {
        top = style->top.getPx();
        autoMask &= ~Top;
    }
    if (!isnan(r)) {
        height = r;
        autoMask &= ~Height;
    } else if (!style->height.isAuto()) {
        height = style->height.getPx();
        autoMask &= ~Height;
    }
    if (!style->bottom.isAuto()) {
        bottom = style->bottom.getPx();
        autoMask &= ~Bottom;
    }
    float leftover = containingBlock->height - getTotalHeight() - top - bottom;
    switch (autoMask & (Top | Height | Bottom)) {
    case Top | Height | Bottom:
        top = -offsetV;
        autoMask &= ~Top;
        // FALL THROUGH
    case Height | Bottom:
        height += leftover;  // Set the max size and do shrink-to-fit later.
        break;
    case Top | Height:
        height += leftover;  // Set the max size and do shrink-to-fit later.
        break;
    case Top | Bottom:
        top = -offsetV;
        bottom += leftover - top;
        break;
    case Top:
        top += leftover;
        break;
    case Height:
        height += leftover;
        break;
    case Bottom:
        bottom += leftover;
        break;
    case 0:
        if (style->marginTop.isAuto() && style->marginBottom.isAuto()) {
            if (0.0f <= leftover)
                marginTop = marginBottom = leftover / 2.0f;
            else {
                marginTop = 0.0f;
                marginBottom = -leftover;
            }
        } else if (style->marginTop.isAuto())
            marginTop = leftover;
        else if (style->marginBottom.isAuto())
            marginBottom = leftover;
        else
            bottom += leftover;
        break;
    }
    return autoMask;
}

unsigned Block::applyAbsoluteMinMaxHeight(const ContainingBlock* containingBlock, float& top, float& bottom, unsigned autoMask)
{
    if (!style->maxHeight.isNone()) {
        float maxHeight = style->maxHeight.getPx();
        if (maxHeight < height)
            autoMask = resolveAbsoluteHeight(containingBlock, top, bottom, maxHeight);
    }
    float minHeight = style->minHeight.getPx();
    if (height < minHeight)
        autoMask = resolveAbsoluteHeight(containingBlock, top, bottom, minHeight);
    return autoMask;
}

void Block::layOutAbsolute(ViewCSSImp* view)
{
    assert(node);
    assert(isAbsolutelyPositioned());
    Element element = getContainingElement(node);
    if (!element)
        return;  // TODO error
    if (!style)
        return;  // TODO error
    style->addBox(this);

#ifndef NDEBUG
    std::u16string tag(interface_cast<html::HTMLElement>(element).getTagName());
    std::u16string id(interface_cast<html::HTMLElement>(element).getId());
#endif

    float savedWidth = width;
    float savedHeight = height;

    setContainingBlock(view);
    const ContainingBlock* containingBlock = &absoluteBlock;
    flags |= style->resolve(view, containingBlock);
    visibility = style->visibility.getValue();
    textAlign = style->textAlign.getValue();

    resolveBackground(view);
    updatePadding();
    updateBorderWidth();

    float left;
    float right;
    unsigned maskH = resolveAbsoluteWidth(containingBlock, left, right);
    applyAbsoluteMinMaxWidth(containingBlock, left, right, maskH);
    float top;
    float bottom;
    unsigned maskV = resolveAbsoluteHeight(containingBlock, top, bottom);
    applyAbsoluteMinMaxHeight(containingBlock, top, bottom, maskV);

    if (CSSDisplayValueImp::isBlockLevel(style->display.getOriginalValue())) {
        // This box is originally a block-level box inside an inline context.
        // Set the static position to the beginning of the next line.
        if (const Box* lineBox = getParentBox()) {  // A root element can be absolutely positioned.
            for (const Box* box = getPreviousSibling(); box; box = box->getPreviousSibling()) {
                if (!box->isAbsolutelyPositioned()) {
                    if (maskV == (Top | Height | Bottom) || maskV == (Top | Bottom))
                        offsetV += lineBox->height + lineBox->getBlankBottom();
                    if (maskH == (Left | Width | Right) || maskH == (Left | Right))
                        offsetH -= box->getTotalWidth();
                }
            }
        }
    }

    if ((flags & (NEED_REFLOW | NEED_CHILD_REFLOW | NEED_REPOSITION)) == NEED_REPOSITION) {
        if (maskH & Width) {
            width = savedWidth;
            if (maskH & Left)
                left = containingBlock->width - getTotalWidth() - right;
            maskH = applyAbsoluteMinMaxWidth(containingBlock, left, right, maskH);
        }
        if (savedWidth == width) {
            if (maskV & Height) {
                float before = height;
                height = savedHeight;
                if (maskV == (Top | Height))
                    top += before - height;
                maskV = applyAbsoluteMinMaxHeight(containingBlock, top, bottom, maskV);
            }
            if (savedHeight == height) {
#ifndef NDEBUG
                if (3 <= getLogLevel())
                    std::cout << "Block::" << __func__ << ": skip reflow for '" << tag << "'\n";
#endif
                layOutAbsoluteEnd(left, top);
                return;
            }
        }
        flags |= NEED_REFLOW;
    }

    FormattingContext* context = updateFormattingContext(0);
    assert(context);

    if (width != savedWidth)
        flags |= NEED_REFLOW;
    else if (!(maskV & Height) && height != savedHeight)
        flags |= NEED_REFLOW;

    layOutInlineBlocks(view);

    if (layOutReplacedElement(view, element, style.get())) {
        maskH &= ~Width;
        maskV &= ~Height;
        // TODO: more conditions...
    } else if (hasInline())
        layOutInline(view, context);
    layOutChildren(view, context);

    if (maskH == (Left | Width) || maskH == (Width | Right)) {
        shrinkToFit(0);
        if (maskH & Left)
            left = containingBlock->width - getTotalWidth() - right;
    }
    // Check 'max-width' and then 'min-width' again.
    maskH = applyAbsoluteMinMaxWidth(containingBlock, left, right, maskH);

    collapseMarginBottom(context);

    if (maskV == (Top | Height) || maskV == (Height | Bottom)) {
        float before = height;
        float totalClearance = 0.0f;
        height = 0;
        for (Box* child = getFirstChild(); child; child = child->getNextSibling()) {
            height += child->getTotalHeight();
            totalClearance += child->getClearance();
        }
        // Note if height is zero, clearances are used only to layout floating boxes,
        // and thus totalClearance should not be added to height.
        if (height != 0.0f)
            height += totalClearance;
        if (maskV == (Top | Height))
            top += before - height;
    }
    // Check 'max-height' and then 'min-height' again.
    maskV = applyAbsoluteMinMaxHeight(containingBlock, top, bottom, maskV);

    // Now that 'height' is fixed, calculate 'left', 'right', 'top', and 'bottom'.
    for (Box* child = getFirstChild(); child; child = child->getNextSibling())
        child->fit(width, 0);

    resolveBackgroundPosition(view, containingBlock);

    restoreFormattingContext(context);
    adjustCollapsedThroughMargins(context);
    layOutAbsoluteEnd(left, top);
}

void Block::layOutAbsoluteEnd(float left, float top)
{
    offsetH += left;
    offsetV += top;

    if (style->getPseudoElementSelectorType() == CSSPseudoElementSelector::Marker) {
        Box* list = getParentBox()->getParentBox();
        if (list->isAnonymous())
            list = list->getParentBox();
        if (list->getParentBox() && list->getParentBox()->style->counterReset.hasCounter()) {
            // cf. http://www.w3.org/TR/css3-lists/#list-style-position-property
            //   The horizontal static position of the marker is such that the
            //   marker's "end" edge is placed against the "start" edge of the
            //   list item's parent.
            list = list->getParentBox();
            offsetH = (list->x + list->getBlankLeft()) - x - getTotalWidth() + getBlankRight();
        } else {
            // While CSS 2.1 does not specify the precise location of the marker box,
            // the marker box appears to be placed at the left side (ltr) of the border edge.
            // cf. border-left-width-applies-to-010 and padding-left-applies-to-010.
            offsetH = (list->x + list->getMarginLeft()) - x - getTotalWidth();
        }
    }

    flags &= ~(NEED_REFLOW | NEED_CHILD_REFLOW | NEED_REPOSITION);
}

void Block::resolveXY(ViewCSSImp* view, float left, float top, Block* clip)
{
    if (!isAnonymous() && style && style->float_.getValue() == CSSFloatValueImp::Right) {
        // cf. http://www.webstandards.org/action/acid2/guide/#row-10-11
        if (getEffectiveTotalWidth() == 0.0f)
            left -= getTotalWidth();
    }

    left += offsetH;
    top += offsetV + getClearance();
    x = left;
    y = top;
    clipBox = clip;
    left += getBlankLeft();
    top += getBlankTop() + topBorderEdge;

    if (isClipped())
        clip = this;
    if (isPositioned()) {
        assert(getStyle());
        getStyle()->getStackingContext()->setClipBox(clipBox);
    }

    if (!childWindow) {
        for (auto child = getFirstChild(); child; child = child->getNextSibling()) {
            child->resolveXY(view, left, top, clip);
            top += child->getTotalHeight() + child->getClearance();
        }
    }
}

void Block::dump(std::string indent)
{
    std::cout << indent << "* block-level box";
    float relativeX = stackingContext ? stackingContext->getRelativeX() : 0.0f;
    float relativeY = stackingContext ? stackingContext->getRelativeY() : 0.0f;
    if (isAnonymous())
        std::cout << " [anonymous]";
    else
        std::cout << " [" << interface_cast<Element>(node).getLocalName() << ']';
    if (3 <= getLogLevel())
        std::cout << " [" << std::hex << flags << ']' << std::dec;
    std::cout << " (" << x + relativeX << ", " << y + relativeY << ") " <<
        "w:" << width << " h:" << height << ' ' <<
        "(" << relativeX << ", " << relativeY <<") ";
    if ((width < scrollWidth || height < scrollHeight) && (parentBox || scrollWidth != 816 || scrollHeight != 1056)) // TODO: Use some constants
        std::cout << "sw:" << scrollWidth << " sh:" << scrollHeight << ' ';
    if (hasClearance())
        std::cout << "c:" << clearance << ' ';
    if (isCollapsedThrough())
        std::cout << "t:" << topBorderEdge << ' ';
    std::cout << "m:" << marginTop << ':' << marginRight << ':' << marginBottom << ':' << marginLeft << ' ' <<
        "p:" << paddingTop << ':' <<  paddingRight << ':'<< paddingBottom<< ':' << paddingLeft << ' ' <<
        "b:" << borderTop << ':' <<  borderRight << ':' << borderBottom<< ':' << borderLeft << ' ' <<
        std::hex << CSSSerializeRGB(backgroundColor) << std::dec << '\n';
    indent += "  ";
    for (Box* child = getFirstChild(); child; child = child->getNextSibling())
        child->dump(indent);
}

}}}}  // org::w3c::dom::bootstrap
