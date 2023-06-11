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

#pragma once

#include "Clip.h"
#include "AutomationEvent.h"
#include "FloatBoundsComponent.h"

// Inherited by clip components in the pattern roll
// and by the automation editor in the bottom panel:
class AutomationEditorBase
{
public:

    virtual ~AutomationEditorBase() = default;

    virtual const Colour &getColour(const AutomationEvent &event) const = 0;
    virtual Rectangle<float> getEventBounds(const AutomationEvent &event, const Clip &clip) const = 0;
    virtual void getBeatValueByPosition(int x, int y, const Clip &clip, float &outValue, float &outBeat) const = 0;
    virtual float getBeatByPosition(int x, const Clip &clip) const = 0;

    // All common stuff for automation event components:
    // first, they maintain connector components between them,
    // so they need a way to know who are their neighbours,
    // also the editor keeps them sorted for faster access,
    // hence the comparator method and some getters
    class EventComponentBase : public virtual FloatBoundsComponent
    {
    public:

        virtual ~EventComponentBase() = default;

        virtual void setNextNeighbour(EventComponentBase *next) = 0;
        virtual void setPreviousNeighbour(EventComponentBase *next) = 0;

        virtual const Clip &getClip() const noexcept = 0;
        virtual const AutomationEvent &getEvent() const noexcept = 0;

        virtual const AutomationEditorBase &getEditor() const noexcept = 0;

        float getBeatByPosition() const
        {
            return this->getEditor().getBeatByPosition(this->getX(), this->getClip());
        }

        // resize all connectors and other helpers
        virtual void updateChildrenBounds() = 0;

        static int compareElements(const EventComponentBase *first,
            const EventComponentBase *second)
        {
            if (first == second) { return 0; }

            const float beatDiff = (first->getEvent().getBeat() + first->getClip().getBeat()) -
                (second->getEvent().getBeat() + second->getClip().getBeat());

            const int diffResult = (beatDiff > 0.f) - (beatDiff < 0.f);
            if (diffResult != 0) { return diffResult; }

            return first->getEvent().getId() - second->getEvent().getId();
        }
    };
};
