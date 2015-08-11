/*
 * Copyright © 2014 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Andreas Pokorny <andreas.pokorny@canonical.com>
 */

#include "libinput_device.h"
#include "libinput_ptr.h"
#include "libinput_device_ptr.h"
#include "input_modifier_utils.h"
#include "evdev_device_detection.h"

#include "mir/input/input_sink.h"
#include "mir/input/input_report.h"
#include "mir/input/device_capability.h"
#include "mir/input/input_device_info.h"
#include "mir/events/event_builders.h"
#include "mir/geometry/displacement.h"
#include "mir/dispatch/dispatchable.h"
#include "mir/fd.h"

#include <libinput.h>
#include <linux/input.h>  // only used to get constants for input reports

#include <cstring>
#include <vector>
#include <chrono>
#include <sstream>

namespace md = mir::dispatch;
namespace mi = mir::input;
namespace mie = mi::evdev;
using namespace std::literals::chrono_literals;

namespace
{
const auto unknown_device_id = MirInputDeviceId{0};

struct DispatchableFd : md::Dispatchable
{
    DispatchableFd(mir::Fd fd, std::function<void()> const& handler)
        : source{fd}, callback{handler}
    {}
    mir::Fd source;
    std::function<void()> callback;
    mir::Fd watch_fd() const override
    {
        return source;
    }

    bool dispatch(md::FdEvents events) override
    {
        if (events & md::FdEvent::error)
            return false;
        if (events & md::FdEvent::readable)
            callback();
        return true;
    }
    md::FdEvents relevant_events() const override
    {
        return md::FdEvent::readable;
    }

};

void null_deleter(MirEvent *) {}
}

mie::LibInputDevice::LibInputDevice(std::shared_ptr<mi::InputReport> const& report,
                                    LibInputPtr library, char const* path)
    : report{report}, lib{std::move(library)},
      dispatchable_fd{std::make_shared<DispatchableFd>(
            mir::Fd(mir::IntOwnedFd{libinput_get_fd(lib.get())}),
            [this]()
            {
                libinput_dispatch(lib.get());
                process_event(libinput_get_event(lib.get()));
            })},
      accumulated_touch_event(nullptr, null_deleter),
      pointer_pos{0, 0},
      modifier_state{0},
      button_state{0}
{
      paths.emplace_back(path);
      devices.emplace_back(mie::make_libinput_device(lib, paths.back().c_str()));
}

mie::LibInputDevice::~LibInputDevice() = default;

std::shared_ptr<mir::dispatch::Dispatchable> mie::LibInputDevice::dispatchable()
{
    return dispatchable_fd;
}

void mie::LibInputDevice::start(InputSink* sink)
{
    this->sink = sink;
}

void mie::LibInputDevice::stop()
{
    sink = nullptr;
    devices.clear();
}

void mie::LibInputDevice::process_event(libinput_event* event)
{
    if (!sink)
        return;

    switch(libinput_event_get_type(event))
    {
    case LIBINPUT_EVENT_KEYBOARD_KEY:
        sink->handle_input(*convert_event(libinput_event_get_keyboard_event(event)));
        break;
    case LIBINPUT_EVENT_POINTER_MOTION:
        sink->handle_input(*convert_motion_event(libinput_event_get_pointer_event(event)));
        break;
    case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
        sink->handle_input(*convert_absolute_motion_event(libinput_event_get_pointer_event(event)));
        break;
    case LIBINPUT_EVENT_POINTER_BUTTON:
        sink->handle_input(*convert_button_event(libinput_event_get_pointer_event(event)));
        break;
    case LIBINPUT_EVENT_POINTER_AXIS:
        sink->handle_input(*convert_axis_event(libinput_event_get_pointer_event(event)));
        break;
    // touch events are processed as a batch of changes over all touch pointts
    case LIBINPUT_EVENT_TOUCH_DOWN:
        add_touch_down_event(libinput_event_get_touch_event(event));
        break;
    case LIBINPUT_EVENT_TOUCH_UP:
        add_touch_up_event(libinput_event_get_touch_event(event));
        break;
    case LIBINPUT_EVENT_TOUCH_MOTION:
        add_touch_motion_event(libinput_event_get_touch_event(event));
        break;
    case LIBINPUT_EVENT_TOUCH_CANCEL:
        // Not yet provided by libinput.
        break;
    case LIBINPUT_EVENT_TOUCH_FRAME:
        sink->handle_input(get_accumulated_touch_event(0ns));
        break;
    default:
        break;
    }
}

mir::EventUPtr mie::LibInputDevice::convert_event(libinput_event_keyboard* keyboard)
{
    auto time = std::chrono::nanoseconds(libinput_event_keyboard_get_time(keyboard));
    auto action = libinput_event_keyboard_get_key_state(keyboard) == LIBINPUT_KEY_STATE_PRESSED ?
                      mir_keyboard_action_down :
                      mir_keyboard_action_up;
    auto code = libinput_event_keyboard_get_key(keyboard);
    report->received_event_from_kernel(time.count(), EV_KEY, code, action);

    auto event = mir::events::make_event(unknown_device_id,
                                         time,
                                         action,
                                         xkb_keysym_t{0},
                                         libinput_event_keyboard_get_key(keyboard),
                                         mie::expand_modifiers(modifier_state));
    if (libinput_event_keyboard_get_key_state(keyboard) == LIBINPUT_KEY_STATE_PRESSED)
    {
        modifier_state |= mie::to_modifier(libinput_event_keyboard_get_key(keyboard));
    }
    else
    {
        modifier_state &= ~mie::to_modifier(libinput_event_keyboard_get_key(keyboard));
    }

    return event;
}

mir::EventUPtr mie::LibInputDevice::convert_button_event(libinput_event_pointer* pointer)
{
    auto time = std::chrono::nanoseconds(libinput_event_pointer_get_time(pointer));
    auto button = libinput_event_pointer_get_button(pointer);
    auto action = (libinput_event_pointer_get_button_state(pointer) == LIBINPUT_BUTTON_STATE_PRESSED)?
        mir_pointer_action_button_down : mir_pointer_action_button_up;


    auto pointer_button = mie::to_pointer_button(button);
    auto relative_x_value = 0.0f;
    auto relative_y_value = 0.0f;
    auto hscroll_value = 0.0f;
    auto vscroll_value = 0.0f;

    report->received_event_from_kernel(time.count(), EV_KEY, pointer_button, action);

    if (action == mir_pointer_action_button_down)
        button_state = MirPointerButton(button_state | uint32_t(pointer_button));
    else
        button_state = MirPointerButton(button_state & ~uint32_t(pointer_button));

    auto event = mir::events::make_event(unknown_device_id,
                                         time,
                                         mie::expand_modifiers(modifier_state),
                                         action,
                                         button_state,
                                         pointer_pos.x.as_float(), pointer_pos.y.as_float(),
                                         hscroll_value, vscroll_value,
                                         relative_x_value,
                                         relative_y_value);

    return event;
}

mir::EventUPtr mie::LibInputDevice::convert_motion_event(libinput_event_pointer* pointer)
{
    auto time = std::chrono::nanoseconds(libinput_event_pointer_get_time(pointer));
    auto action = mir_pointer_action_motion;
    auto hscroll_value = 0.0f;
    auto vscroll_value = 0.0f;

    report->received_event_from_kernel(time.count(), EV_REL, 0, 0);

    mir::geometry::Displacement movement{
        libinput_event_pointer_get_dx(pointer),
        libinput_event_pointer_get_dy(pointer)};
    pointer_pos = pointer_pos + movement;

    sink->confine_pointer(pointer_pos);

    auto event = mir::events::make_event(unknown_device_id,
                                         time,
                                         mie::expand_modifiers(modifier_state),
                                         action,
                                         button_state,
                                         pointer_pos.x.as_float(), pointer_pos.y.as_float(),
                                         hscroll_value, vscroll_value,
                                         movement.dx.as_float(), movement.dy.as_float());

    return event;
}

mir::EventUPtr mie::LibInputDevice::convert_absolute_motion_event(libinput_event_pointer* pointer)
{
    // a pointing device that emits absolute coordinates
    auto time = std::chrono::nanoseconds(libinput_event_pointer_get_time(pointer));
    auto action = mir_pointer_action_motion;
    auto hscroll_value = 0.0f;
    auto vscroll_value = 0.0f;

    report->received_event_from_kernel(time.count(), EV_ABS, 0, 0);
    auto old_pointer_pos = pointer_pos;
    pointer_pos = mir::geometry::Point{
        libinput_event_pointer_get_absolute_x(pointer),
        libinput_event_pointer_get_absolute_y(pointer)};
    auto movement = pointer_pos - old_pointer_pos;

    sink->confine_pointer(pointer_pos);

    auto event = mir::events::make_event(unknown_device_id,
                                         time,
                                         mie::expand_modifiers(modifier_state),
                                         action,
                                         button_state,
                                         pointer_pos.x.as_float(), pointer_pos.y.as_float(),
                                         hscroll_value, vscroll_value,
                                         movement.dx.as_float(), movement.dy.as_float());
    return event;
}

mir::EventUPtr mie::LibInputDevice::convert_axis_event(libinput_event_pointer* pointer)
{
    auto time = std::chrono::nanoseconds(libinput_event_pointer_get_time(pointer));
    auto action = mir_pointer_action_motion;
    auto relative_x_value = 0.0f;
    auto relative_y_value = 0.0f;
    auto hscroll_value = libinput_event_pointer_has_axis(pointer, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL)
        ? libinput_event_pointer_get_axis_value(pointer, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL)
        : 0.0f;
    auto vscroll_value = libinput_event_pointer_has_axis(pointer, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL)
        ? libinput_event_pointer_get_axis_value(pointer, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL)
        : 0.0f;

    report->received_event_from_kernel(time.count(), EV_REL, 0, 0);
    auto event = mir::events::make_event(
        unknown_device_id, time, mie::expand_modifiers(modifier_state), action, button_state, pointer_pos.x.as_float(),
        pointer_pos.y.as_float(),
        hscroll_value, vscroll_value,
        relative_x_value, relative_y_value);
    return event;
}

MirEvent& mie::LibInputDevice::get_accumulated_touch_event(std::chrono::nanoseconds timestamp)
{
    if (!accumulated_touch_event)
    {
        report->received_event_from_kernel(timestamp.count(), EV_SYN, 0, 0);
        accumulated_touch_event = mir::events::make_event(unknown_device_id, timestamp, mie::expand_modifiers(modifier_state));
    }

    return *accumulated_touch_event;
}

void mie::LibInputDevice::add_touch_down_event(libinput_event_touch* touch)
{
    auto time = std::chrono::nanoseconds(libinput_event_touch_get_time(touch));
    auto& event = get_accumulated_touch_event(time);

    MirTouchId id = libinput_event_touch_get_slot(touch);
    auto action = mir_touch_action_down;
    auto tool = mir_touch_tooltype_finger; // TODO make libinput indicate tool type
    float size = 1.0f;
    float pressure = 1.0f;  // TODO libinput_event_touch_get_pressure(touch),
    float major = 1.0f; // TODO libinput_event_touch_get_major(touch),
    float minor = 1.0f; // TODO libinput_event_touch_get_minor(touch),

    // TODO extend for touch screens that provide orientation
    mir::events::add_touch(event, id, action, tool,
                           libinput_event_touch_get_x(touch),
                           libinput_event_touch_get_y(touch),
                           pressure,
                           major,
                           minor,
                           size);
}

void mie::LibInputDevice::add_touch_up_event(libinput_event_touch* touch)
{
    auto time = std::chrono::nanoseconds(libinput_event_touch_get_time(touch));
    auto& event = get_accumulated_touch_event(time);
    MirTouchId id = libinput_event_touch_get_slot(touch);
    auto action = mir_touch_action_up;
    auto tool = mir_touch_tooltype_finger; // TODO make libinput indicate tool type

    float pressure = 0.0f;
    float major = 0.0f;
    float minor = 0.0f;
    float size = 0.0f;
    // TODO extend for touch screens that provide orientation and major/minor
    mir::events::add_touch(event, id, action, tool,
                           libinput_event_touch_get_x(touch),
                           libinput_event_touch_get_y(touch),
                           pressure,
                           major,
                           minor,
                           size);
}

void mie::LibInputDevice::add_touch_motion_event(libinput_event_touch* touch)
{
    auto time = std::chrono::nanoseconds(libinput_event_touch_get_time(touch));
    auto& event = get_accumulated_touch_event(time);

    MirTouchId id = libinput_event_touch_get_slot(touch);
    auto action = mir_touch_action_change;
    auto tool = mir_touch_tooltype_finger; // TODO make libinput indicate tool type
    float size = 1.0f;
    float pressure = 1.0f;  // TODO libinput_event_touch_get_pressure(touch),
    float major = 1.0f; // TODO libinput_event_touch_get_major(touch),
    float minor = 1.0f; // TODO libinput_event_touch_get_minor(touch),

    // TODO extend for touch screens that provide orientation
    mir::events::add_touch(event, id, action, tool,
                           libinput_event_touch_get_x(touch),
                           libinput_event_touch_get_y(touch),
                           pressure,
                           major,
                           minor,
                           size);
}

mi::InputDeviceInfo mie::LibInputDevice::get_device_info()
{
    auto const& dev = devices.front();
    std::string name = libinput_device_get_name(dev.get());
    std::stringstream unique_id(name);
    unique_id << '-' << libinput_device_get_sysname(dev.get()) << '-' <<
        libinput_device_get_id_vendor(dev.get()) << '-' <<
        libinput_device_get_id_product(dev.get());
    mi::DeviceCapabilities caps;

    for (auto const& path : paths)
        caps |= mie::detect_device_capabilities(path.c_str());

    return mi::InputDeviceInfo{0, name, unique_id.str(), caps};
}


libinput_device*  mie::LibInputDevice::device()
{
    return devices.front().get();
}

void mie::LibInputDevice::open_device_of_group(char const* path)
{
    paths.emplace_back(path);
    devices.emplace_back(mie::make_libinput_device(lib, path));
}
