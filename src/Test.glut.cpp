/*
 * Copyright 2011-2013 Esrille Inc.
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

#include <GL/glew.h>
#include <GL/freeglut.h>

#include <thread>
#include <mutex>
#include <vector>

#include "WindowImp.h"
#include "Test.util.h"
#include "http/HTTPConnection.h"

using namespace org::w3c::dom::bootstrap;
using namespace org::w3c::dom;

html::Window window(0);

namespace
{

std::mutex mutex;

// threadFlags
const unsigned MainThread = 0x01;
__thread unsigned threadFlags;
std::vector<GLuint> texturesToDelete;

void deleteTextures() {
    std::lock_guard<std::mutex> lock(mutex);
    if (!texturesToDelete.empty()) {
        glDeleteTextures(texturesToDelete.size(), texturesToDelete.data());
        texturesToDelete.clear();
    }
}

}

void deleteTexture(unsigned texture) {
    std::lock_guard<std::mutex> lock(mutex);
    texturesToDelete.push_back(texture);
}

void reshape(int w, int h)
{
    glViewport(0, 0, w, h);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, h, 0, -1000.0, 1.0);

    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    if (WindowImp* imp = static_cast<WindowImp*>(window.self()))
        imp->setSize(w, h);
}

void display()
{
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    if (WindowImp* imp = static_cast<WindowImp*>(window.self())) {
        imp->render(0);
#ifndef NDEBUG
        GLint depth;
        glGetIntegerv(GL_MODELVIEW_STACK_DEPTH, &depth);
        assert(depth == 1);

        GLfloat m[16];
        glGetFloatv(GL_MODELVIEW_MATRIX, m);
        float x = m[12];
        float y = m[13];
        assert(x == 0.0f && y == 0.0f);
#endif
    }

    deleteTextures();

    glutSwapBuffers();  // This would block until the sync happens
}

unsigned getCharKeyCode(int key)
{
    // US Qwerty
    static unsigned map[] = {
        0, 0, 0, 0, 0, 0, 0, 0, 8, 9, 0, 0, 12, 13, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 8, 9, 0, 27, 0, 0, 0, 0,
        32, 49 /* ! */, 222 /* " */, 51 /* # */, 52 /* $ */, 53 /* % */, 55 /* & */, 222 /* ' */,
        57 /* ( */, 48 /* ) */, 56 /* * */, 187 /* + */, 188 /* , */, 189 /* - */, 190 /* . */, 191 /* / */,
        48 /* 0 */, 49 /* 1 */, 50 /* 2 */, 51 /* 3 */, 52 /* 4 */, 53 /* 5 */, 54 /* 6 */, 55 /* 7 */,
        56 /* 8 */, 57 /* 9 */, 186 /* : */, 186 /* ; */, 188 /* < */, 187 /* = */, 190 /* > */, 191 /* ? */,
        50 /* @ */, 65 /* A */, 66 /* B */, 67 /* C */, 68 /* D */, 69 /* E */, 70 /* F */, 71 /* G */,
        72 /* H */, 73 /* I */, 74 /* J */, 75 /* K */, 76 /* L */, 77 /* M */, 78 /* N */, 79 /* O */,
        80 /* P */, 81 /* Q */, 82 /* R */, 83 /* S */, 84 /* T */, 85 /* U */, 86 /* V */, 87 /* W */,
        88 /* X */, 89 /* Y */, 90 /* Z */, 219 /* [ */, 220 /* \ */, 221 /* ] */, 54 /* ^ */, 189 /* _ */,
        192 /* ` */, 65 /* a */, 66 /* b */, 67 /* c */, 68 /* d */, 69 /* e */, 70 /* f */, 71 /* g */,
        72 /* h */, 73 /* i */, 74 /* j */, 75 /* k */, 76 /* l */, 77 /* m */, 78 /* n */, 79 /* o */,
        80 /* p */, 81 /* q */, 82 /* r */, 83 /* s */, 84 /* t */, 85 /* u */, 86 /* v */, 87 /* w */,
        88 /* x */, 89 /* y */, 90 /* z */, 219 /* { */, 220 /* | */, 221 /* } */, 192 /* ~ */, 46 /* DEL */
    };
    static_assert(sizeof map /sizeof map[0] == 128, "invalid map");
    return (0 <= key && key <= 127) ? map[key] : 0;
}

void keyboard(unsigned char key, int x, int y)
{
    if (WindowImp* imp = static_cast<WindowImp*>(window.self()))
        imp->keydown(isprint(key) ? key : 0, getCharKeyCode(key), glutGetModifiers());
}

void keyboardUp(unsigned char key, int x, int y)
{
    if (WindowImp* imp = static_cast<WindowImp*>(window.self()))
        imp->keyup(isprint(key) ? key : 0, getCharKeyCode(key), glutGetModifiers());
}

unsigned getSpecialKeyCode(int key)
{
    switch (key) {
    case GLUT_KEY_F1:
        return 112;
    case GLUT_KEY_F2:
        return 113;
    case GLUT_KEY_F3:
        return 114;
    case GLUT_KEY_F4:
        return 115;
    case GLUT_KEY_F5:
        return 116;
    case GLUT_KEY_F6:
        return 117;
    case GLUT_KEY_F7:
        return 118;
    case GLUT_KEY_F8:
        return 119;
    case GLUT_KEY_F9:
        return 120;
    case GLUT_KEY_F10:
        return 121;
    case GLUT_KEY_F11:
        return 122;
    case GLUT_KEY_F12:
        return 123;
    case GLUT_KEY_LEFT:
        return 37;
    case GLUT_KEY_UP:
        return 38;
    case GLUT_KEY_RIGHT:
        return 39;
    case GLUT_KEY_DOWN:
        return 40;
    case GLUT_KEY_PAGE_UP:
        return 33;
    case GLUT_KEY_PAGE_DOWN:
        return 34;
    case GLUT_KEY_HOME:
        return 36;
    case GLUT_KEY_END:
        return 35;
    case GLUT_KEY_INSERT:
        return 45;
    case 109:  // Num Lock
        return 144;
    default:
        return 0;
    }
}

void special(int key, int x, int y)
{
    unsigned keycode = getSpecialKeyCode(key);
    if (keycode) {
        if (WindowImp* imp = static_cast<WindowImp*>(window.self()))
            imp->keydown(0, keycode, glutGetModifiers());
    }
}

void specialUp(int key, int x, int y)
{
    unsigned keycode = getSpecialKeyCode(key);
    if (keycode) {
        if (WindowImp* imp = static_cast<WindowImp*>(window.self()))
            imp->keyup(0, keycode, glutGetModifiers());
    }
}

void mouse(int button, int state, int x, int y)
{
    if (WindowImp* imp = static_cast<WindowImp*>(window.self()))
        imp->mouse(button, state, x, y, glutGetModifiers());
}

void mouseMove(int x, int y)
{
    if (WindowImp* imp = static_cast<WindowImp*>(window.self()))
        imp->mouseMove(x, y, glutGetModifiers());
}

void entry(int state)
{
    if (state == GLUT_LEFT) {
        if (WindowImp* imp = static_cast<WindowImp*>(window.self())) {
            // Note glutGetModifiers() cannot be used outside an input callback
            // TODO: Keep modifiers status.
            imp->mouseMove(-1, -1, 0 /* glutGetModifiers() */);
        }
    }
}

void timer(int value)
{
    HttpConnectionManager::getInstance().poll();    // TODO: This line should not be necessary.
    if (WindowImp* imp = static_cast<WindowImp*>(window.self())) {
        if (imp->poll())
            glutPostRedisplay();
    }
    glutTimerFunc(50, timer, 0);
    // TODO: do GC here or maybe in the idle proc
}

void init(int* argc, char* argv[], int width, int height)
{
    threadFlags = MainThread;

    glutInit(argc, argv);
    glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_DEPTH | GLUT_STENCIL);
    if (width <= 0 || height <= 0) {
        int w = glutGet(GLUT_SCREEN_WIDTH);
        int h = glutGet(GLUT_SCREEN_HEIGHT);
        width = std::min(std::max(780, w - 64), 816);
        height = std::min(h - 128, width * 22 / 17);
    }
    glutInitWindowSize(width, height);
    glutCreateWindow(argv[0]);

    GLenum err = glewInit();
    if (err != GLEW_OK) {
        std::cout << "error: " << glewGetErrorString(err) << "\n";
        exit(EXIT_FAILURE);
    }

    if (!GLEW_EXT_framebuffer_object && !GLEW_ARB_framebuffer_object) {
        std::cout << "error: Neither EXT_framebuffer_object nor ARB_framebuffer_object extension is supported by the installed OpenGL driver.\n";
        exit(EXIT_FAILURE);
    }
    glutReshapeFunc(reshape);
    glutDisplayFunc(display);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glEnable(GL_TEXTURE_2D);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glutKeyboardFunc(keyboard);
    glutKeyboardUpFunc(keyboardUp);
    glutSpecialFunc(special);
    glutSpecialUpFunc(specialUp);
    glutMouseFunc(mouse);
    glutMotionFunc(mouseMove);
    glutPassiveMotionFunc(mouseMove);
    glutEntryFunc(entry);
    glutTimerFunc(50, timer, 0);
    glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_CONTINUE_EXECUTION);

    glClearStencil(0x00);
    glEnable(GL_STENCIL_TEST);
    GLint stencilBits;
    glGetIntegerv(GL_STENCIL_BITS, &stencilBits);
    if (stencilBits < 8) {
        std::cout << "error: The number of the OpenGL stencil bits needs to be greater than or equal to 8. Current: " << stencilBits << ".\n";
        exit(EXIT_FAILURE);
    }
}

bool isMainThread()
{
    return threadFlags & MainThread;
}
