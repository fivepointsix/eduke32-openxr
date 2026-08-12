#ifndef DUKEVR_OPENXR_H
#define DUKEVR_OPENXR_H

#include <stdint.h>

/* The OpenXR bridge is deliberately small: BRender continues to own the
 * scene and its eye render targets, while this module owns only the runtime
 * session, frame timing, swapchain images, and submission. */
int DukeVROpenXR_Enabled(void);
int DukeVROpenXR_Initialize(void);
int DukeVROpenXR_BeginFrame(void);
int DukeVROpenXR_FrameActive(void);
int DukeVROpenXR_BeginEyeRender(int eye);
void DukeVROpenXR_EndEyeRender(void);
int DukeVROpenXR_BeginHudRender(void);
void DukeVROpenXR_EndHudRender(void);
void DukeVROpenXR_SetHudMenuScale(int scaled);
int DukeVROpenXR_CurrentEye(void);
int DukeVROpenXR_SceneFrameActive(void);
int DukeVROpenXR_PresentMirror(int eye, int width, int height);
void DukeVROpenXR_MarkSceneFrame(void);
int DukeVROpenXR_ConsumeSceneFrame(void);
void DukeVROpenXR_MarkMonoFrame(void);
int DukeVROpenXR_GetEyePose(int eye, float orientation_xyzw[4], float position_xyz[3]);
int DukeVROpenXR_GetEyeFov(int eye, float* angle_left, float* angle_right,
    float* angle_up, float* angle_down);
void DukeVROpenXR_SetRenderOrientationResidual(float yaw_radians,
    float pitch_radians, float roll_radians);
void DukeVROpenXR_GetRenderOrientationResidual(float* yaw_radians,
    float* pitch_radians, float* roll_radians);
void DukeVROpenXR_SetRenderPositionResidual(float x_build, float y_build,
    float z_build);
void DukeVROpenXR_GetRenderPositionResidual(float* x_build, float* y_build,
    float* z_build);
int DukeVROpenXR_GetEyeDimensions(int eye, int* width, int* height);
int DukeVROpenXR_GetSceneDimensions(int* width, int* height);
uint32_t DukeVROpenXR_GetEyeTexture(int eye);
uint32_t DukeVROpenXR_GetHudTexture(void);
void DukeVROpenXR_MarkHudFrame(void);
int DukeVROpenXR_SubmitTexture(void* source);
int DukeVROpenXR_SubmitDesktopFrame(int width, int height);
void DukeVROpenXR_EndFrame(void);
void DukeVROpenXR_Shutdown(void);

/* Legacy LibOVR-shaped entry points used by the original DukeVR patch. */
struct DukeVROpenXRState;
typedef struct DukeVROpenXRState *DukeVROpenXRSession;

#endif
