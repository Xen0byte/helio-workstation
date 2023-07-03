/*
    This file is part of Helio Workstation.

    Helio is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Helio is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Helio. If not, see <http://www.gnu.org/licenses/>.
*/

#include "Common.h"
#include "AutomationStepEventComponent.h"
#include "AutomationStepEventsConnector.h"
#include "AutomationSequence.h"

AutomationStepEventComponent::AutomationStepEventComponent(AutomationEditorBase &editor,
    const AutomationEvent &event, const Clip &clip) :
    editor(editor),
    event(event),
    clip(clip)
{
    this->setInterceptsMouseClicks(true, false);
    this->setMouseClickGrabsKeyboardFocus(false);
    this->setPaintingIsUnclipped(true);
}

void AutomationStepEventComponent::paint(Graphics &g)
{
    const bool prevDownState = this->prevEventHolder != nullptr ?
        this->prevEventHolder->getEvent().isPedalDownEvent() :
        Globals::Defaults::onOffControllerState;

    constexpr auto threshold = AutomationStepEventComponent::minLengthInBeats * 3.f;

    const bool isCloseToPrevious = this->prevEventHolder != nullptr &&
        (this->getEvent().getBeat() - this->prevEventHolder->getEvent().getBeat()) <= threshold;

    const bool isCloseToNext = this->nextEventHolder != nullptr &&
        (this->nextEventHolder->getEvent().getBeat() - this->getEvent().getBeat()) <= threshold;

    constexpr auto r = AutomationStepEventComponent::pointOffset;
    constexpr auto d = r * 2.f;
    constexpr auto top = r + AutomationStepEventComponent::marginTop;
    const float bottom = this->floatLocalBounds.getHeight() - r - AutomationStepEventComponent::marginBottom;
    const float left = this->floatLocalBounds.getX();
    const float right = jmax(left + 0.5f, this->floatLocalBounds.getWidth() - r);

    const auto mainColour = this->editor.getColour(this->event);
    g.setColour(mainColour);

    const auto lineColour = mainColour.withMultipliedAlpha(0.75f);

    if (this->event.isPedalDownEvent() && !prevDownState)
    {
        g.fillEllipse(right - r + 0.5f, bottom - r, d, d);

        g.setColour(lineColour);
        const bool compactMode = isCloseToPrevious && this->getWidth() <= 3;
        if (!compactMode)
        {
            g.drawLine(right + 0.5f, top, right + 0.5f, bottom - d + 1.f);
            g.drawHorizontalLine(int(top), left, right + 0.5f);
        }
    }
    else if (this->event.isPedalUpEvent() && prevDownState)
    {
        g.fillEllipse(right - r, top - r, d, d);

        g.setColour(lineColour);
        const bool compactMode = isCloseToNext && this->getWidth() <= 3;
        g.drawLine(right, top + d, right, compactMode ? bottom - d + 1.f : bottom);
        g.drawHorizontalLine(int(bottom), left, compactMode ? right - d : right + 0.5f);
    }
    else if (this->event.isPedalDownEvent() && prevDownState)
    {
        g.fillEllipse(right - r + 0.5f, bottom - r, d, d);

        g.setColour(lineColour);
        g.drawHorizontalLine(int(bottom), left, right - d);
    }
    else if (this->event.isPedalUpEvent() && !prevDownState)
    {
        g.fillEllipse(right - r, top - r, d, d);

        g.setColour(lineColour);
        g.drawHorizontalLine(int(top), left, right - d);
    }

    if (this->isHighlighted)
    {
        g.fillRect(0, this->getHeight() - 6, this->getWidth(), 4);
    }
}

void AutomationStepEventComponent::moved()
{
    this->updateChildrenBounds();
}

void AutomationStepEventComponent::parentHierarchyChanged()
{
    if (this->getParentComponent() != nullptr)
    {
        this->recreateConnector();
    }
}

void AutomationStepEventComponent::mouseDown(const MouseEvent &e)
{
    if (e.mods.isLeftButtonDown())
    {
        this->event.getSequence()->checkpoint();
        this->dragger.startDraggingComponent(this, e);
        this->isDragging = true;
    }
}

void AutomationStepEventComponent::mouseDrag(const MouseEvent &e)
{
    if (e.mods.isLeftButtonDown() && this->isDragging)
    {
        this->setMouseCursor(MouseCursor::DraggingHandCursor);
        this->dragger.dragComponent(this, e, nullptr);
        const float newBeat = this->editor.getBeatByPosition(this->getX() + this->getWidth(), this->clip);
        this->drag(newBeat);
    }
}

void AutomationStepEventComponent::mouseUp(const MouseEvent &e)
{
    this->setMouseCursor(MouseCursor::NormalCursor);

    if (e.mods.isLeftButtonDown())
    {
        if (this->isDragging)
        {
            this->isDragging = false;
        }
    }
    else if (e.mods.isRightButtonDown())
    {
        auto *sequence = static_cast<AutomationSequence *>(this->event.getSequence());
        if (sequence->size() > 1) // no empty automation tracks please
        {
            sequence->checkpoint();
            sequence->remove(this->event, true);
        }
    }
}

void AutomationStepEventComponent::mouseEnter(const MouseEvent &e)
{
    this->isHighlighted = true;
    this->repaint();
}

void AutomationStepEventComponent::mouseExit(const MouseEvent &e)
{
    this->isHighlighted = false;
    this->repaint();
}

void AutomationStepEventComponent::drag(float targetBeat)
{
    float newRoundBeat = targetBeat;
    auto *sequence = static_cast<AutomationSequence *>(this->event.getSequence());

    // ограничим перемещение отрезком между двумя соседними компонентами
    const int myIndex = sequence->indexOfSorted(&this->event);
    const bool hasPreviousEvent = myIndex > 0;
    const bool hasNextEvent = myIndex < (sequence->size() - 1);

    if (hasPreviousEvent)
    {
        newRoundBeat = jmax(sequence->getUnchecked(myIndex - 1)->getBeat() +
            AutomationStepEventComponent::minLengthInBeats, newRoundBeat);
    }

    if (hasNextEvent)
    {
        newRoundBeat = jmin(sequence->getUnchecked(myIndex + 1)->getBeat() -
            AutomationStepEventComponent::minLengthInBeats, newRoundBeat);
    }

    const float newRoundDeltaBeat = (newRoundBeat - this->event.getBeat());

    if (fabs(newRoundDeltaBeat) > 0.01)
    {
        sequence->change(this->event, this->event.withBeat(newRoundBeat), true);
    }
    else
    {
        this->setFloatBounds(this->editor.getEventBounds(this->event, this->clip));
        this->updateChildrenBounds();
    }
}

void AutomationStepEventComponent::dragByDelta(float deltaBeat)
{
    this->drag(this->getEvent().getBeat() + deltaBeat);
}

void AutomationStepEventComponent::recreateConnector()
{
    this->connector = make<AutomationStepEventsConnector>(this,
        this->nextEventHolder, this->event.isPedalDownEvent());

    assert(this->getParentComponent() != nullptr);
    this->getParentComponent()->addAndMakeVisible(this->connector.get());
    this->updateConnector();
}

void AutomationStepEventComponent::updateConnector()
{
    this->connector->resizeToFit(this->event.isPedalDownEvent());
}

//===----------------------------------------------------------------------===//
// EventComponentBase
//===----------------------------------------------------------------------===//

void AutomationStepEventComponent::setNextNeighbour(EventComponentBase *next)
{
    if (next != nullptr)
    {
        next->repaint();
    }

    if (next == this->nextEventHolder)
    {
        this->updateChildrenBounds();
        return;
    }

    this->nextEventHolder = next;
    this->recreateConnector();
}

void AutomationStepEventComponent::setPreviousNeighbour(EventComponentBase *prev)
{
    if (prev == this->prevEventHolder)
    {
        return;
    }

    this->prevEventHolder = prev;
}