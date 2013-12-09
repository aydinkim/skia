
/*
 * Copyright 2013 The Servo Project Developers
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "gl/SkNativeSharedGLContext.h"
#include "gl/GrGLUtil.h"
#include <dlfcn.h>
#include <EGL/eglext.h>

enum {
    HAL_PIXEL_FORMAT_RGBA_8888          = 1,
    HAL_PIXEL_FORMAT_RGBX_8888          = 2,
    HAL_PIXEL_FORMAT_RGB_888            = 3,
    HAL_PIXEL_FORMAT_RGB_565            = 4,
    HAL_PIXEL_FORMAT_BGRA_8888          = 5,
    HAL_PIXEL_FORMAT_RGBA_5551          = 6,
    HAL_PIXEL_FORMAT_RGBA_4444          = 7,
};

enum {
    /* buffer is never read in software */
    GRALLOC_USAGE_SW_READ_NEVER   = 0x00000000,
    /* buffer is rarely read in software */
    GRALLOC_USAGE_SW_READ_RARELY  = 0x00000002,
    /* buffer is often read in software */
    GRALLOC_USAGE_SW_READ_OFTEN   = 0x00000003,
    /* mask for the software read values */
    GRALLOC_USAGE_SW_READ_MASK    = 0x0000000F,

    /* buffer is never written in software */
    GRALLOC_USAGE_SW_WRITE_NEVER  = 0x00000000,
    /* buffer is never written in software */
    GRALLOC_USAGE_SW_WRITE_RARELY = 0x00000020,
    /* buffer is never written in software */
    GRALLOC_USAGE_SW_WRITE_OFTEN  = 0x00000030,
    /* mask for the software write values */
    GRALLOC_USAGE_SW_WRITE_MASK   = 0x000000F0,

    /* buffer will be used as an OpenGL ES texture */
    GRALLOC_USAGE_HW_TEXTURE      = 0x00000100,
    /* buffer will be used as an OpenGL ES render target */
    GRALLOC_USAGE_HW_RENDER       = 0x00000200,
    /* buffer will be used by the 2D hardware blitter */
    GRALLOC_USAGE_HW_2D           = 0x00000400,
    /* buffer will be used with the framebuffer device */
    GRALLOC_USAGE_HW_FB           = 0x00001000,
    /* mask for the software usage bit-mask */
    GRALLOC_USAGE_HW_MASK         = 0x00001F00,
};

SkNativeSharedGLContext::SkNativeSharedGLContext(GrGLNativeContext& nativeContext)
    : fContext(EGL_NO_CONTEXT)
    , fDisplay(nativeContext.fDisplay)
    , fSurface(EGL_NO_SURFACE)
    , fEGLImage(NULL)
    , mHandle(NULL)
    , fGrContext(NULL)
    , fGL(NULL)
    , fFBO(0)
    , fTextureID(0)
    , fDepthStencilBufferID(0) {
        void* handle = dlopen("/system/lib/libui.so", RTLD_LAZY);
        fGraphicBufferGetNativeBuffer = (pfnGraphicBufferGetNativeBuffer)dlsym(handle, "_ZNK7android13GraphicBuffer15getNativeBufferEv");
        fGraphicBufferCtor = (pfnGraphicBufferCtor)dlsym(handle, "_ZN7android13GraphicBufferC1Ejjij");

}

SkNativeSharedGLContext::~SkNativeSharedGLContext() {
    if (fGL) {
        SK_GL_NOERRCHECK(*this, DeleteFramebuffers(1, &fFBO));
        SK_GL_NOERRCHECK(*this, DeleteTextures(1, &fTextureID));
        SK_GL_NOERRCHECK(*this, DeleteRenderbuffers(1, &fDepthStencilBufferID));
    }
    SkSafeUnref(fGL);
    this->destroyGLContext();
    if (fGrContext) {
        fGrContext->Release();
    }
}

void SkNativeSharedGLContext::destroyGLContext() {
    if (fDisplay) {
        eglMakeCurrent(fDisplay, 0, 0, 0);

        if (fContext) {
            eglDestroyContext(fDisplay, fContext);
            fContext = EGL_NO_CONTEXT;
        }

        if (fSurface) {
            eglDestroySurface(fDisplay, fSurface);
            fSurface = EGL_NO_SURFACE;
        }

        //TODO should we close the display?
        fDisplay = EGL_NO_DISPLAY;
    }
}

const GrGLInterface* SkNativeSharedGLContext::createGLContext(const int width, const int height) {
    EGLint numConfigs;
    static const EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };

    EGLConfig surfaceConfig;
    eglChooseConfig(fDisplay, configAttribs, &surfaceConfig, 1, &numConfigs);

    static const EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    static const EGLint surfaceAttribs[] = {
        EGL_WIDTH, width,
        EGL_HEIGHT, height,
        EGL_NONE
    };
    fContext = eglCreateContext(fDisplay, surfaceConfig, EGL_NO_CONTEXT, contextAttribs);
    fSurface = eglCreatePbufferSurface(fDisplay, surfaceConfig, surfaceAttribs);

    eglMakeCurrent(fDisplay, fSurface, fSurface, fContext);

    const GrGLInterface* interface = GrGLCreateNativeInterface();
    if (!interface) {
        SkDebugf("Failed to create gl interface");
        this->destroyGLContext();
        return NULL;
    }
    return interface;
}

bool SkNativeSharedGLContext::init(int width, int height) {
    if (fGL) {
        fGL->unref();
        this->destroyGLContext();
    }

    fGL = this->createGLContext(width, height);
    if (fGL) {
        const GrGLubyte* temp;

        GrGLBinding bindingInUse = GrGLGetBindingInUse(this->gl());

        if (!fGL->validate(bindingInUse) || !fExtensions.init(bindingInUse, fGL)) {
            fGL = NULL;
            this->destroyGLContext();
            return false;
        }

        SK_GL_RET(*this, temp, GetString(GR_GL_VERSION));
        const char* versionStr = reinterpret_cast<const char*>(temp);
        GrGLVersion version = GrGLGetVersionFromString(versionStr);

        // clear any existing GL erorrs
        GrGLenum error;
        do {
            SK_GL_RET(*this, error, GetError());
        } while (GR_GL_NO_ERROR != error);

        if (!mHandle) {
            mHandle = malloc(1024/*GRAPHIC_BUFFER_SIZE*/);
            fGraphicBufferCtor(mHandle, width, height, HAL_PIXEL_FORMAT_RGBX_8888, GRALLOC_USAGE_SW_READ_OFTEN|GRALLOC_USAGE_SW_WRITE_OFTEN|GRALLOC_USAGE_HW_TEXTURE|GRALLOC_USAGE_HW_RENDER|GRALLOC_USAGE_HW_2D); 
            if (!fGraphicBufferCtor) {
                fprintf(stderr, "fGraphicBufferCtor doesn't exist!\n");
            }
        }

        SK_GL(*this, GenFramebuffers(1, &fFBO));
        SK_GL(*this, BindFramebuffer(GR_GL_FRAMEBUFFER, fFBO));
        SK_GL(*this, GenTextures(1, &fTextureID));
        SK_GL(*this, BindTexture(GR_GL_TEXTURE_2D, fTextureID));
        SK_GL(*this, TexImage2D(GR_GL_TEXTURE_2D, 0,
                                GR_GL_RGBA,
                                width, height, 0,
                                GR_GL_RGBA, GR_GL_UNSIGNED_BYTE, 
                                NULL));

        SK_GL(*this, TexParameteri(GR_GL_TEXTURE_2D, GR_GL_TEXTURE_WRAP_S, GR_GL_CLAMP_TO_EDGE));
        SK_GL(*this, TexParameteri(GR_GL_TEXTURE_2D, GR_GL_TEXTURE_WRAP_T, GR_GL_CLAMP_TO_EDGE));
        SK_GL(*this, TexParameteri(GR_GL_TEXTURE_2D, GR_GL_TEXTURE_MAG_FILTER, GR_GL_LINEAR));
        SK_GL(*this, TexParameteri(GR_GL_TEXTURE_2D, GR_GL_TEXTURE_MIN_FILTER, GR_GL_LINEAR));
        SK_GL(*this, FramebufferTexture2D(GR_GL_FRAMEBUFFER,
                                          GR_GL_COLOR_ATTACHMENT0,
                                          GR_GL_TEXTURE_2D,
                                          fTextureID, 0));

        SK_GL(*this, GenRenderbuffers(1, &fDepthStencilBufferID));
        SK_GL(*this, BindRenderbuffer(GR_GL_RENDERBUFFER, fDepthStencilBufferID));

        // Some drivers that support packed depth stencil will only succeed
        // in binding a packed format an FBO. However, we can't rely on packed
        // depth stencil being available.
        bool supportsPackedDepthStencil;
        if (kES2_GrGLBinding == bindingInUse) {
            supportsPackedDepthStencil = this->hasExtension("GL_OES_packed_depth_stencil");
        } else {
            supportsPackedDepthStencil = version >= GR_GL_VER(3,0) ||
                                         this->hasExtension("GL_EXT_packed_depth_stencil") ||
                                         this->hasExtension("GL_ARB_framebuffer_object");
        }

        if (supportsPackedDepthStencil) {
            // ES2 requires sized internal formats for RenderbufferStorage
            // On Desktop we let the driver decide.
            GrGLenum format = kES2_GrGLBinding == bindingInUse ?
                                    GR_GL_DEPTH24_STENCIL8 :
                                    GR_GL_DEPTH_STENCIL;
            SK_GL(*this, RenderbufferStorage(GR_GL_RENDERBUFFER,
                                             format,
                                             width, height));
            SK_GL(*this, FramebufferRenderbuffer(GR_GL_FRAMEBUFFER,
                                                 GR_GL_DEPTH_ATTACHMENT,
                                                 GR_GL_RENDERBUFFER,
                                                 fDepthStencilBufferID));
        } else {
            GrGLenum format = kES2_GrGLBinding == bindingInUse ?
                                    GR_GL_STENCIL_INDEX8 :
                                    GR_GL_STENCIL_INDEX;
            SK_GL(*this, RenderbufferStorage(GR_GL_RENDERBUFFER,
                                             format,
                                             width, height));
        }
        SK_GL(*this, FramebufferRenderbuffer(GR_GL_FRAMEBUFFER,
                                             GR_GL_STENCIL_ATTACHMENT,
                                             GR_GL_RENDERBUFFER,
                                             fDepthStencilBufferID));
        SK_GL(*this, Viewport(0, 0, width, height));
        SK_GL(*this, ClearStencil(0));
        SK_GL(*this, Clear(GR_GL_STENCIL_BUFFER_BIT));
        

        SK_GL_RET(*this, error, GetError());
        GrGLenum status;
        SK_GL_RET(*this, status, CheckFramebufferStatus(GR_GL_FRAMEBUFFER));

        if (GR_GL_FRAMEBUFFER_COMPLETE != status ||
            GR_GL_NO_ERROR != error) {
            fFBO = 0;
            fTextureID = 0;
            fDepthStencilBufferID = 0;
            fGL->unref();
            fGL = NULL;
            this->destroyGLContext();
            return false;
        } else {
            return true;
        }
    }
    return false;
}

GrContext *SkNativeSharedGLContext::getGrContext() {
    if (fGrContext) {
        return fGrContext;
    } else {
        GrBackendContext p3dctx = reinterpret_cast<GrBackendContext>(this->gl());
        fGrContext = GrContext::Create(kOpenGL_GrBackend, p3dctx);
        if (fGrContext == NULL) {
            return NULL;
        }
        // No need to AddRef; the GrContext is created with refcount = 1.
        return fGrContext;
    }
}

GrGLSharedSurface SkNativeSharedGLContext::stealSurface() {
    if (fGL && fFBO) {
        SK_GL(*this, BindFramebuffer(GR_GL_FRAMEBUFFER, fFBO));
        //SK_GL(*this, FramebufferTexture2D(GR_GL_FRAMEBUFFER,
        //            GR_GL_COLOR_ATTACHMENT0,
        //            GR_GL_TEXTURE_2D,
        //            0,
        //            0));
    }

    int viewport[4];
    SK_GL(*this, GetIntegerv(GR_GL_VIEWPORT, viewport));
    int width = viewport[2], height = viewport[3];
    //glPixelStorei(GL_PACK_ALIGNMENT, 1);
    //glReadPixels(0, 0, width, height, GR_GL_RGBA, GR_GL_UNSIGNED_BYTE, fSharedSurface);
    //EGLNativePixmapType fNativePixmap = fSharedSurface;

/*
    SK_GL(*this, GenRenderbuffers(1, &fColorBuffer));
    SK_GL(*this, BindRenderbuffer(GR_GL_RENDERBUFFER, fColorBuffer));
    SK_GL(*this, RenderbufferStorage(GR_GL_RENDERBUFFER,
                GR_GL_RGBA,
                width, height));

    SK_GL(*this, FramebufferRenderbuffer(GR_GL_FRAMEBUFFER,
                        GR_GL_COLOR_ATTACHMENT0,
                        GR_GL_RENDERBUFFER,
                        fColorBuffer));

    fprintf(stderr, "Color buffer_pre: %p\n", fColorBuffer);
    SK_GL(*this, Flush());
    SK_GL(*this, BindFramebuffer(GR_GL_FRAMEBUFFER, 0));
    fprintf(stderr, "Color buffer_post: %p\n", fColorBuffer);
*/

    glBindTexture(GL_TEXTURE_2D, 0);
    GrGLuint texture;
    SK_GL(*this, GenTextures(1, &texture));
    SK_GL(*this, BindTexture(GR_GL_TEXTURE_2D, texture));
    SK_GL(*this, TexImage2D(GR_GL_TEXTURE_2D, 0,
                GR_GL_RGBA,
                width, height, 0,
                GR_GL_RGBA, GR_GL_UNSIGNED_BYTE,
                NULL));
    //SK_GL(*this, CopyTexSubImage2D(GR_GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height));
    SK_GL(*this, FramebufferTexture2D(GR_GL_FRAMEBUFFER,
                GR_GL_COLOR_ATTACHMENT0,
                GR_GL_TEXTURE_2D,
                texture, 0));
    SK_GL(*this, Flush());

    fprintf(stderr, "passing stealSurface!\n", texture);
    //glBindTexture(GL_TEXTURE_2D, 0);
    EGLSyncKHR fence = eglCreateSyncKHR(fDisplay, EGL_SYNC_FENCE_KHR, NULL);
    EGLint result = eglClientWaitSyncKHR(fDisplay, fence, EGL_SYNC_FLUSH_COMMANDS_BIT_KHR, EGL_FOREVER_KHR);

    if(result == EGL_FALSE) {
        fprintf(stderr, "error waiting for fence!\n");
    }

    EGLint value = 0;
    result = eglGetSyncAttribKHR(fDisplay, fence, EGL_SYNC_STATUS_KHR, &value);
    if(result == EGL_FALSE) {
        fprintf(stderr, "error getting sync attrib!\n");
    }

    if(value == EGL_SIGNALED_KHR) {
        EGLint eglImgAttrs[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE, EGL_NONE };
        fEGLImage = eglCreateImageKHR(fDisplay, fContext, EGL_GL_TEXTURE_2D_KHR, (EGLClientBuffer)fTextureID, eglImgAttrs);
    }


    if (!fEGLImage) {
        fprintf(stderr, "fEGLImage doesn't exist!\n");
    }

    eglDestroySyncKHR(fDisplay, fence);
    /*if(mHandle) {
        EGLint eglImgAttrs[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE, EGL_NONE };
        void* nativeBuffer = fGraphicBufferGetNativeBuffer(mHandle);
        if (!fGraphicBufferGetNativeBuffer) {
            fprintf(stderr, "fGraphicBufferGetNativeBuffer doesn't exist!\n");
        }
        if (nativeBuffer) {
            fEGLImage = eglCreateImageKHR(fDisplay, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, (EGLClientBuffer)nativeBuffer, eglImgAttrs);
            if (!fEGLImage) {
                fprintf(stderr, "fEGLImage doesn't exist!\n");
            }
        }
        else {
            fprintf(stderr, "nativebuffer doesn't exist!\n");
        }
    }
    else {
        fprintf(stderr, "mHandle doesn't exist!\n");
    }*/
    fTextureID = 0;
    fSurface = NULL;
    return fEGLImage;
}

void SkNativeSharedGLContext::makeCurrent() const {
    if (!eglMakeCurrent(fDisplay, fSurface, fSurface, fContext)) {
        SkDebugf("Could not set the context.\n");
    }
}

void SkNativeSharedGLContext::flush() const {
    this->makeCurrent();

    fprintf(stderr, "called glFinish!!!!!\n");
    SK_GL(*this, Finish());
}
