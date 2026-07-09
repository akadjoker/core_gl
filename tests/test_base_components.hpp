 #pragma once

#include "test_common.hpp"
#include "scene/ByteArray.hpp"
#include "scene/IO.hpp"
#include "scene/Filesystem.hpp"
#include "scene/AssetManager.hpp"

#include <cstdio>
#include <cstring>

static const char* kBaseVS = R"(#version 430 core
layout(location = 0) in vec2 a_pos;
void main()
{
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)";

static const char* kBaseFS = R"(#version 430 core
out vec4 OutColor;
uniform vec3 u_color;
void main()
{
    OutColor = vec4(u_color, 1.0);
}
)";

inline int test_base_components(int maxFrames)
{
    int pass = 0, fail = 0;

    // --- io default (desktop fopen)
    io::registerDefaultDesktop();

    // --- ByteArray write/read round-trip
    {
        scene::ByteArray ba;
        ba.writeU32(0xDEADBEEF);
        ba.writeF32(3.14f);
        ba.writeString("hello");
        ba.resetCursor();

        gl::u32 u = 0;
        gl::f32 f = 0.0f;
        char buf[64] = {};
        bool ok = ba.readU32(u) && ba.readF32(f) && ba.readString(buf, sizeof(buf));
        ok = ok && (u == 0xDEADBEEF);
        ok = ok && (f > 3.13f && f < 3.15f);
        ok = ok && (strcmp(buf, "hello") == 0);

        printf("[ByteArray] round-trip u32/f32/string ... %s\n", ok ? "PASS" : "FAIL");
        ok ? ++pass : ++fail;
    }

    // --- ByteArray big endian
    {
        scene::ByteArray ba;
        ba.setBigEndian(true);
        ba.writeU16(0x1234);
        ba.resetCursor();

        gl::u8 b[2];
        ba.readBytes(b, 2);

        bool ok = (b[0] == 0x12 && b[1] == 0x34);
        printf("[ByteArray] big-endian byte order ...... %s\n", ok ? "PASS" : "FAIL");
        ok ? ++pass : ++fail;
    }

    // --- Filesystem: write temp file, read it back
    {
        std::FILE* f = std::fopen("/tmp/coregl_test_fs.txt", "wb");
        if (f)
        {
            std::fputs("coregl_fs_test", f);
            std::fclose(f);
        }

        fs::getFilesystem().addFolder("/tmp");

        char resolved[256];
        bool ok = fs::getFilesystem().resolvePath("coregl_test_fs.txt", resolved, sizeof(resolved));
        ok = ok && fs::getFilesystem().exists("coregl_test_fs.txt");

        scene::ByteArray data;
        ok = ok && fs::getFilesystem().readText("coregl_test_fs.txt", data);
        ok = ok && (strcmp((const char*)data.data(), "coregl_fs_test") == 0);

        printf("[Filesystem] addFolder + resolve + read . %s\n", ok ? "PASS" : "FAIL");
        ok ? ++pass : ++fail;
    }

    // --- AssetManager: GL context needed for shader/texture
    TestApp app;
    if (!app.Create("coregl - base components", 256, 256))
    {
        printf("[AssetManager] SKIP (no GL context)\n");
        printf("\nresult: %d passed, %d failed\n", pass, fail);
        return (fail > 0) ? 1 : 0;
    }

    // --- AssetManager: shader from string
    {
        assets::AssetManager::instance().init();
        gl::Shader* sh = assets::AssetManager::instance().loadShaderFromString(
            "test", kBaseVS, kBaseFS);

        bool ok = sh && sh->IsValid();
        gl::Shader* cached = assets::AssetManager::instance().getShader("test");
        ok = ok && (cached == sh);
        ok = ok && (assets::AssetManager::instance().shaderCount() == 1);

        printf("[AssetManager] loadShaderFromString ...... %s\n", ok ? "PASS" : "FAIL");
        ok ? ++pass : ++fail;
    }

    // --- AssetManager: write a fake PNG (1x1 red) and load it
    {
        static const unsigned char k1x1RedPNG[] = {
            0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
            0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
            0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
            0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53,
            0xDE, 0x00, 0x00, 0x00, 0x0C, 0x49, 0x44, 0x41,
            0x54, 0x08, 0xD7, 0x63, 0xF8, 0xCF, 0xC0, 0x00,
            0x00, 0x00, 0x03, 0x00, 0x01, 0x5B, 0x70, 0x1C,
            0xEA, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E,
            0x44, 0xAE, 0x42, 0x60, 0x82
        };
        std::FILE* f = std::fopen("/tmp/coregl_test_tex.png", "wb");
        if (f)
        {
            std::fwrite(k1x1RedPNG, 1, sizeof(k1x1RedPNG), f);
            std::fclose(f);
        }

        gl::Texture* tex = assets::AssetManager::instance().loadTexture("red", "coregl_test_tex.png");
        bool ok = tex && tex->IsValid();
        ok = ok && (tex->GetWidth() == 1 && tex->GetHeight() == 1);

        printf("[AssetManager] loadTexture (1x1 PNG) ..... %s\n", ok ? "PASS" : "FAIL");
        ok ? ++pass : ++fail;
    }

    // --- Visual: render a quad with the loaded shader (optional)
    {
        gl::Shader* sh = assets::AssetManager::instance().getShader("test");
        gl::Texture* tex = assets::AssetManager::instance().getTexture("red");

        float verts[] = {
            -0.5f, -0.5f,
             0.5f, -0.5f,
            -0.5f,  0.5f,
             0.5f,  0.5f,
        };
        gl::Buffer vbo;
        vbo.Allocate(gl::BufferType::ARRAY, verts, sizeof(verts), gl::UsageType::STATIC_DRAW);

        const gl::VertexAttrib layout[] = {
            {gl::VertexAttribType::FLOAT, 2, 0, false},
        };
        gl::VertexArray vao;
        vao.AddVertexBuffer(vbo, layout, 1, 2 * sizeof(float));

        gl::i32 locColor = sh ? sh->GetLocation("u_color") : -1;

        int frame = 0;
        while (app.PollEvents())
        {
            app.BeginFrame();
            gl::Renderer::ClearColor(0.05f, 0.05f, 0.08f, 1.0f);
            gl::Renderer::Clear(true, true);

            if (sh && tex)
            {
                sh->Bind();
                sh->SetVec3(locColor, 0.8f, 0.3f, 0.3f);
                tex->Bind(0);
                vao.Bind();
                gl::Renderer::Draw(gl::RenderPrimitive::TRIANGLE_STRIP, 4);
            }

            app.EndFrame();
            ++frame;
            if (maxFrames > 0 && frame >= maxFrames) break;
        }
    }

    assets::AssetManager::instance().release();
    app.Destroy();

    printf("\nresult: %d passed, %d failed\n", pass, fail);
    return (fail > 0) ? 1 : 0;
}