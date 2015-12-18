/*
 * Copyright © 2015 Canonical Ltd.
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
 * Authored by: Robert Carr <robert.carr@canonical.com>
 */
#include "default_client_buffer_stream_factory.h"
#include "buffer_stream.h"
#include "perf_report.h"
#include "logging/perf_report.h"
#include "lttng/perf_report.h"

namespace mcl = mir::client;
namespace mclr = mir::client::rpc;
namespace ml = mir::logging;
namespace mp = mir::protobuf;

namespace
{

std::shared_ptr<mcl::PerfReport>
make_perf_report(std::shared_ptr<ml::Logger> const& logger)
{
    // TODO: It seems strange that this directly uses getenv
    const char* report_target = getenv("MIR_CLIENT_PERF_REPORT");
    if (report_target && !strncmp(report_target, "log", strlen(report_target)))
    {
        return std::make_shared<mcl::logging::PerfReport>(logger);
    }
    else if (report_target && !strncmp(report_target, "lttng", strlen(report_target)))
    {
        return std::make_shared<mcl::lttng::PerfReport>();
    }
    else
    {
        return std::make_shared<mcl::NullPerfReport>();
    }
}

size_t get_nbuffers_from_env()
{
    //TODO: (kdub) cannot set nbuffers == 2 in production until we have client-side
    //      timeout-based overallocation for timeout-based framedropping.
    const char* nbuffers_opt = getenv("MIR_CLIENT_NBUFFERS");
    if (nbuffers_opt && !strncmp(nbuffers_opt, "2", strlen(nbuffers_opt)))
        return 2u;
    return 3u;
}
}

mcl::DefaultClientBufferStreamFactory::DefaultClientBufferStreamFactory(
    std::shared_ptr<mcl::ClientPlatform> const& client_platform,
    std::shared_ptr<ml::Logger> const& logger)
    : client_platform(client_platform),
      logger(logger),
      nbuffers(get_nbuffers_from_env())
{
}

std::shared_ptr<mcl::ClientBufferStream> mcl::DefaultClientBufferStreamFactory::make_consumer_stream(
    MirConnection* connection, mclr::DisplayServer& server,
    mp::BufferStream const& protobuf_bs, std::string const& surface_name, geometry::Size size)
{
    return std::make_shared<mcl::BufferStream>(
        connection, server, mcl::BufferStreamMode::Consumer, client_platform,
        protobuf_bs, make_perf_report(logger), surface_name, size, nbuffers);
}

std::shared_ptr<mcl::ClientBufferStream> mcl::DefaultClientBufferStreamFactory::make_producer_stream(
    MirConnection* connection, mclr::DisplayServer& server,
    mp::BufferStream const& protobuf_bs, std::string const& surface_name, geometry::Size size)
{
    return std::make_shared<mcl::BufferStream>(
        connection, server, mcl::BufferStreamMode::Producer, client_platform,
        protobuf_bs, make_perf_report(logger), surface_name, size, nbuffers);
}


mcl::ClientBufferStream* mcl::DefaultClientBufferStreamFactory::make_producer_stream(
    MirConnection* connection, mclr::DisplayServer& server,
    mp::BufferStreamParameters const& params, mir_buffer_stream_callback, void*)
{
    return new mcl::BufferStream(
        connection, server, client_platform, params, make_perf_report(logger), nbuffers);
}
