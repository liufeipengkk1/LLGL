/*
 * LinuxGLRenderContext.cpp
 * 
 * This file is part of the "LLGL" project (Copyright (c) 2015 by Lukas Hermanns)
 * See "LICENSE.txt" for license information.
 */

#include "../../GLRenderContext.h"
#include "../../../../Platform/Linux/LinuxWindow.h"
#include <LLGL/Platform/NativeHandle.h>
#include <LLGL/Log.h>


namespace LLGL
{


void GLRenderContext::Present()
{
    glXSwapBuffers(context_.display, context_.wnd);
}

bool GLRenderContext::GLMakeCurrent(GLRenderContext* renderContext)
{
    if (renderContext)
    {
        const auto& ctx = renderContext->context_;
        return glXMakeCurrent(ctx.display, ctx.wnd, ctx.glc);
    }
    else
        return glXMakeCurrent(nullptr, 0, 0);
    return false;
}


/*
 * ======= Private: =======
 */

void GLRenderContext::GetNativeContextHandle(NativeContextHandle& windowContext)
{
    /* Open X11 display */
    windowContext.display = XOpenDisplay(nullptr);
    if (!windowContext.display)
        throw std::runtime_error("failed to open X11 display");

    windowContext.parentWindow  = DefaultRootWindow(windowContext.display);
    windowContext.screen        = DefaultScreen(windowContext.display);

    /* Create XVisualInfo and Colormap structures */
    int visualAttribs[] = { GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, None };

    windowContext.visual    = glXChooseVisual(windowContext.display, windowContext.screen, visualAttribs);
    windowContext.colorMap  = XCreateColormap(windowContext.display, windowContext.parentWindow, windowContext.visual->visual, AllocNone);
}

void GLRenderContext::CreateContext(GLRenderContext* sharedRenderContext)
{
    GLXContext glcShared = (sharedRenderContext != nullptr ? sharedRenderContext->context_.glc : nullptr);
    
    /* Get X11 display, window, and visual information */
    auto& window = static_cast<const LinuxWindow&>(GetWindow());
    NativeHandle nativeHandle;
    window.GetNativeHandle(&nativeHandle);
    
    context_.display    = nativeHandle.display;
    context_.wnd        = nativeHandle.window;
    context_.visual     = nativeHandle.visual;
    
    if (!context_.display || !context_.wnd || !context_.visual)
        throw std::invalid_argument("failed to create OpenGL context on X11 client, due to missing arguments");
    
    /* Create OpenGL context with X11 lib */
    context_.glc        = glXCreateContext(context_.display, context_.visual, glcShared, GL_TRUE);
    
    /* Make new OpenGL context current */
    if (glXMakeCurrent(context_.display, context_.wnd, context_.glc) != True)
        Log::StdErr() << "failed to make OpenGL render context current (glXMakeCurrent)" << std::endl;
}

void GLRenderContext::DeleteContext()
{
    glXDestroyContext(context_.display, context_.glc);
}


} // /namespace LLGL



// ================================================================================