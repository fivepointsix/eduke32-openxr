#ifndef DUKEVR_OVR_CAPI_GL_COMPAT_H
#define DUKEVR_OVR_CAPI_GL_COMPAT_H

/*
 * DukeVR keeps the original Oculus-shaped call sites so the 2016 renderer
 * remains usable.  This header is a deliberately small LibOVR compatibility
 * surface; its implementation is backed by OpenXR in source/dukevr_openxr.c.
 */
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#include <stdint.h>
#include <windows.h>
/* The OpenXR platform header only needs the COM interface declaration for
 * a few optional Win32 extension typedefs.  Keep the compatibility header
 * usable with EDuke32's C++ compilation mode even when COM headers are not
 * pulled in by the renderer include order. */
#ifndef __IUnknown_FWD_DEFINED__
typedef struct IUnknown IUnknown;
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

typedef int32_t ovrResult;
typedef int32_t ovrBool;
typedef void *ovrSession;
typedef void *ovrMirrorTexture;
typedef struct DukeVROVRSwapChain *ovrTextureSwapChain;
typedef struct { uint64_t Reserved[2]; } ovrGraphicsLuid;

#define ovrTrue  1
#define ovrFalse 0
#define OVR_SUCCESS(result) ((result) >= 0)
#define ovrSuccess 0
#define ovrSuccess_NotVisible 1000

/* Oculus' GL format values intentionally mirror the corresponding OpenGL
 * enums used by the old renderer. */
#define OVR_FORMAT_R8G8B8A8_UNORM       0x8058
#define OVR_FORMAT_R8G8B8A8_UNORM_SRGB  0x8C43

typedef struct { float x, y, z; } ovrVector3f;
typedef struct { float x, y, z, w; } ovrQuatf;
typedef struct { ovrVector3f Position; ovrQuatf Orientation; } ovrPosef;
typedef struct { ovrPosef ThePose; } ovrPoseStatef;
typedef struct { ovrPoseStatef HeadPose; unsigned int StatusFlags; } ovrTrackingState;
typedef struct { double DisplayMidpointSeconds; } ovrFrameTiming;

typedef struct {
    float LeftTan, RightTan, UpTan, DownTan;
} ovrFovPort;

typedef struct {
    ovrVector3f HmdToEyeOffset;
    ovrFovPort Fov;
} ovrEyeRenderDesc;

typedef struct {
    int Type;
    struct { int w, h; } Resolution;
    ovrFovPort DefaultEyeFov[2];
} ovrHmdDesc;

typedef struct { float x, y; } ovrVector2f;
/* The old source uses x/y for positions and w/h for extents while treating
 * both as the same Oculus vector type. */
typedef struct { int x, y, w, h; } ovrVector2i;
typedef struct { ovrVector2i Pos, Size; } ovrRecti;

typedef struct {
    ovrVector2f Thumbstick[2];
    float IndexTrigger[2];
    unsigned int Buttons;
} ovrInputState;

typedef struct {
    int Type;
    int ArraySize;
    int Width, Height;
    int MipLevels;
    int SampleCount;
    int Format;
    int StaticImage;
} ovrTextureSwapChainDesc;

typedef struct { int Width, Height, Format; } ovrMirrorTextureDesc;

typedef struct {
    int Type;
    unsigned int Flags;
} ovrLayerHeader;

typedef struct {
    ovrLayerHeader Header;
    ovrTextureSwapChain ColorTexture[2];
    ovrRecti Viewport[2];
    ovrFovPort Fov[2];
    ovrPosef RenderPose[2];
} ovrLayerEyeFov;

typedef struct {
    ovrLayerHeader Header;
    ovrTextureSwapChain ColorTexture;
    ovrRecti Viewport;
    ovrVector2f QuadSize;
    ovrPosef QuadPoseCenter;
} ovrLayerQuad;

typedef struct {
    float HmdSpaceToWorldScaleInMeters;
    ovrVector3f HmdToEyeOffset[2];
} ovrViewScaleDesc;

typedef enum { ovrEye_Left = 0, ovrEye_Right = 1 } ovrEyeType;

#define ovrHmd_DK1 1
#define ovrHmd_DK2 6
#define ovrHmd_CV1 3
#define ovrTexture_2D 0

#define ovrControllerType_None   0u
#define ovrControllerType_XBox   0x00000001u
#define ovrControllerType_Remote 0x00000002u

#define ovrButton_A          (1u << 0)
#define ovrButton_B          (1u << 1)
#define ovrButton_X          (1u << 2)
#define ovrButton_Y          (1u << 3)
#define ovrButton_LShoulder  (1u << 4)
#define ovrButton_RShoulder  (1u << 5)
#define ovrButton_LThumb     (1u << 6)
#define ovrButton_RThumb     (1u << 7)
#define ovrButton_Enter      (1u << 8)
#define ovrButton_Back       (1u << 9)
#define ovrButton_Up         (1u << 10)
#define ovrButton_Right      (1u << 11)
#define ovrButton_Down       (1u << 12)
#define ovrButton_Left       (1u << 13)

#define ovrStatus_PositionTracked (1u << 0)
#define ovrLayerType_Disabled 0
#define ovrLayerType_EyeFov   1
#define ovrLayerType_Quad     2
#define ovrLayerFlag_TextureOriginAtBottomLeft (1u << 0)
#define ovrLayerFlag_HeadLocked (1u << 1)

ovrResult ovr_Initialize(const void *params);
void ovr_Shutdown(void);
ovrResult ovr_Create(ovrSession *session, ovrGraphicsLuid *luid);
void ovr_Destroy(ovrSession session);
ovrHmdDesc ovr_GetHmdDesc(ovrSession session);
ovrEyeRenderDesc ovr_GetRenderDesc(ovrSession session, ovrEyeType eye, ovrFovPort fov);
ovrResult ovr_GetEyePoses(ovrSession session, uint64_t frameIndex, ovrBool latencyMarker,
    const ovrVector3f hmdToEyeOffset[2], ovrPosef outEyePoses[2], double *timing);
ovrResult ovr_CalcEyePoses(ovrPosef headPose, const ovrVector3f hmdToEyeOffset[2], ovrPosef outEyePoses[2]);
double ovr_GetPredictedDisplayTime(ovrSession session, uint64_t frameIndex);
ovrTrackingState ovr_GetTrackingState(ovrSession session, double absTime, ovrBool latencyMarker);
ovrResult ovr_RecenterTrackingOrigin(ovrSession session);
#define ovrHmd_GetTrackingState ovr_GetTrackingState

ovrResult ovr_CreateTextureSwapChainGL(ovrSession session, const ovrTextureSwapChainDesc *desc,
    ovrTextureSwapChain *outChain);
ovrResult ovr_DestroyTextureSwapChain(ovrSession session, ovrTextureSwapChain chain);
ovrResult ovr_GetTextureSwapChainLength(ovrSession session, ovrTextureSwapChain chain, int *length);
ovrResult ovr_GetTextureSwapChainCurrentIndex(ovrSession session, ovrTextureSwapChain chain, int *index);
ovrResult ovr_GetTextureSwapChainBufferGL(ovrSession session, ovrTextureSwapChain chain, int index, unsigned int *texId);
ovrResult ovr_CommitTextureSwapChain(ovrSession session, ovrTextureSwapChain chain);
ovrResult ovr_CreateMirrorTextureGL(ovrSession session, const ovrMirrorTextureDesc *desc, ovrMirrorTexture *outMirror);
ovrResult ovr_GetMirrorTextureBufferGL(ovrSession session, ovrMirrorTexture mirror, unsigned int *texId);
ovrResult ovr_DestroyMirrorTexture(ovrSession session, ovrMirrorTexture mirror);
ovrResult ovr_SubmitFrame(ovrSession session, uint64_t frameIndex, const ovrViewScaleDesc *viewScale,
    ovrLayerHeader const * const *layerPtrList, unsigned int layerCount);

unsigned int ovr_GetConnectedControllerTypes(ovrSession session);
ovrResult ovr_GetInputState(ovrSession session, unsigned int controllerType, ovrInputState *inputState);
ovrResult ovr_SetControllerVibration(ovrSession session, unsigned int controllerType, float frequency, float amplitude);

#endif
