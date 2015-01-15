/*
 * Copyright © 2014 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored By: Alan Griffiths <alan@octopull.co.uk>
 */

#ifndef MIR_SCENE_SESSION_COORDINATOR_H_
#define MIR_SCENE_SESSION_COORDINATOR_H_

#include "mir/frontend/surface_id.h"

#include "mir_toolkit/common.h"

#include <memory>

namespace mir
{
// TODO Much of this interface should be rewritten in terms of scene type
// TODO frontend types are retained temporarily while restructuring
namespace frontend
{
class EventSink;
class PromptSession;
class Session;
class Surface;
}

namespace scene
{
class Session;
class Surface;
class SurfaceCreationParameters;
class PromptSessionCreationParameters;

class SessionCoordinator
{
public:
    virtual void focus_next() = 0;
    virtual std::weak_ptr<Session> focussed_application() const = 0;
    virtual void set_focus_to(std::shared_ptr<Session> const& focus) = 0;

    virtual std::shared_ptr<frontend::Session> open_session(
        pid_t client_pid,
        std::string const& name,
        std::shared_ptr<frontend::EventSink> const& sink) = 0;

    virtual void close_session(std::shared_ptr<frontend::Session> const& session)  = 0;

    virtual void handle_surface_created(std::shared_ptr<frontend::Session> const& session) = 0;

    virtual std::shared_ptr<frontend::PromptSession> start_prompt_session_for(std::shared_ptr<frontend::Session> const& session,
                                                                  PromptSessionCreationParameters const& params) = 0;
    virtual void add_prompt_provider_for(std::shared_ptr<frontend::PromptSession> const& prompt_session,
                                                                  std::shared_ptr<frontend::Session> const& session) = 0;
    virtual void stop_prompt_session(std::shared_ptr<frontend::PromptSession> const& prompt_session) = 0;

    virtual frontend::SurfaceId create_surface(std::shared_ptr<frontend::Session> const& session, SurfaceCreationParameters const& params) = 0;
    virtual void destroy_surface(std::shared_ptr<frontend::Session> const& session, frontend::SurfaceId surface) = 0;

    virtual int set_surface_attribute(
        std::shared_ptr<frontend::Session> const& session,
        frontend::SurfaceId surface_id,
        MirSurfaceAttrib attrib,
        int value) = 0;

    virtual int get_surface_attribute(
        std::shared_ptr<frontend::Session> const& session,
        frontend::SurfaceId surface_id,
        MirSurfaceAttrib attrib) = 0;

protected:
    SessionCoordinator() = default;
    virtual ~SessionCoordinator() = default;
    SessionCoordinator(SessionCoordinator const&) = delete;
    SessionCoordinator& operator=(SessionCoordinator const&) = delete;
};

}
}

#endif /* MIR_SCENE_SESSION_COORDINATOR_H_ */
