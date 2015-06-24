 /*
 * Copyright © 2015 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Andreas Pokorny <andreas.pokorny@gmail.com>
 */

#ifndef MIR_INPUT_LEGACY_INPUT_DISPATCHABLE_H_
#define MIR_INPUT_LEGACY_INPUT_DISPATCHABLE_H_

#include "mir/dispatch/dispatchable.h"
#include "mir/dispatch/action_queue.h"

namespace mir
{
namespace input
{

struct LegacyInputDispatchable : mir::dispatch::Dispatchable
{
    virtual void start() = 0;
};

struct NullLegacyInputDispatchable : LegacyInputDispatchable
{
    void start() override {};
    mir::Fd watch_fd() const override { return aq.watch_fd();};
    bool dispatch(mir::dispatch::FdEvents events) override { return aq.dispatch(events); }
    mir::dispatch::FdEvents relevant_events() const override{ return aq.relevant_events(); }
    mir::dispatch::ActionQueue aq;
};

}
}

#endif

