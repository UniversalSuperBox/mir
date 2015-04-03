/*
 * Copyright © 2015 Canonical Ltd.
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
 * Authored By: Kevin DuBois <kevin.dubois@canonical.com>
 */

#include "server_example_adorning_renderer.h"
#include "mir/graphics/display_buffer.h"
#include "mir/graphics/buffer.h"
#include "mir/compositor/scene_element.h"
#include <GLES2/gl2.h>

namespace me = mir::examples;
namespace mg = mir::graphics;
namespace mc = mir::compositor;

bool make_current(mg::DisplayBuffer& db)
{
    db.make_current();
    return true;
}

me::AdorningRenderer::Shader::Shader(GLchar const* const* src, GLuint type) :
    shader(glCreateShader(type))
{
    glShaderSource(shader, 1, src, 0);
    glCompileShader(shader);
}

me::AdorningRenderer::Shader::~Shader()
{
    glDeleteShader(shader);
}

me::AdorningRenderer::Program::Program(Shader& vertex, Shader& fragment) :
    program(glCreateProgram())
{
    glAttachShader(program, vertex.shader);
    glAttachShader(program, fragment.shader);
    glLinkProgram(program);
}

me::AdorningRenderer::Program::~Program()
{
    glDeleteProgram(program);
}

me::AdorningRenderer::AdorningRenderer(mg::DisplayBuffer& display_buffer) :
    db{display_buffer},
    vert_shader_src{
        "attribute vec4 vPosition;"
        "uniform vec2 pos;"
        "uniform vec2 scale;"
        "attribute vec2 uvCoord;"
        "varying vec2 texcoord;"
        "void main() {"
        "   mat4 m;"
        "    m[0] = vec4(scale.x,     0.0, 0.0, pos.x);"
        "    m[1] = vec4(    0.0, scale.y, 0.0, pos.y);"
        "    m[2] = vec4(    0.0,     0.0, 1.0, 0.0);"
        "    m[3] = vec4(    0.0,     0.0, 0.0, 1.0);"
        "   gl_Position = vPosition * m;"
        "   texcoord = uvCoord.xy;"
        "}"
    },
    frag_shader_src{
        "precision mediump float;"
        "varying vec2 texcoord;"
        "uniform sampler2D tex;"
        "uniform float alpha;"
        "void main() {"
        "   gl_FragColor = texture2D(tex, texcoord) * alpha;"
        "}"
    },
    current(make_current(db)),
    vertex(&vert_shader_src, GL_VERTEX_SHADER),
    fragment(&frag_shader_src, GL_FRAGMENT_SHADER),
    program(vertex,  fragment)
{
    glUseProgram(program.program);
    vPositionAttr = glGetAttribLocation(program.program, "vPosition");
    glVertexAttribPointer(vPositionAttr, 4, GL_FLOAT, GL_FALSE, 0, vertex_data);
    uvCoord = glGetAttribLocation(program.program, "uvCoord");
    glVertexAttribPointer(uvCoord, 2, GL_FLOAT, GL_FALSE, 0, uv_data);
    posUniform = glGetUniformLocation(program.program, "pos");
    glClearColor(0.8, 0.5, 0.8, 1.0); //light purple
    scaleUniform = glGetUniformLocation(program.program, "scale");
    alphaUniform = glGetUniformLocation(program.program, "alpha");

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void me::AdorningRenderer::composite(compositor::SceneElementSequence&& scene_sequence)
{
    //note: If what should be drawn is expressible as a SceneElementSequence,
    //      mg::DisplayBuffer::post_renderables_if_optimizable() should be used,
    //      to give the the display hardware a chance at an optimized render of
    //      the scene. In this example though, we want some custom elements, so
    //      we'll always use GLES.
    db.make_current();

    auto display_width  = db.view_area().size.width.as_float();
    auto display_height = db.view_area().size.height.as_float();

    glUseProgram(program.program);
    glClear( GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT );
    
    for(auto& element : scene_sequence)
    {
        //courteously inform the client that its rendered
        //if something is not to be rendered, mc::SceneElementSequence::occluded() should be called 
        element->rendered();

        auto const renderable = element->renderable();
        float width  = renderable->screen_position().size.width.as_float();
        float height = renderable->screen_position().size.height.as_float();
        float x = renderable->screen_position().top_left.x.as_float();
        float y = renderable->screen_position().top_left.y.as_float();
        float scale[2] {
            width/display_width * 2,
            height/display_height * -2};
        float position[2] {
            (x / display_width * 2.0f) - 1.0f,
            1.0f - (y / display_height * 2.0f)
        };
        float const plane_alpha = renderable->alpha();
        if (renderable->shaped() || plane_alpha < 1.0)
            glEnable(GL_BLEND);
        else
            glDisable(GL_BLEND);

        glUniform2fv(posUniform, 1, position);
        glUniform2fv(scaleUniform, 1, scale);
        glUniform1fv(alphaUniform, 1, &plane_alpha);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        renderable->buffer()->gl_bind_to_texture();

        glEnableVertexAttribArray(vPositionAttr);
        glEnableVertexAttribArray(uvCoord);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableVertexAttribArray(uvCoord);
        glDisableVertexAttribArray(vPositionAttr);
    }

    db.gl_swap_buffers();
}
