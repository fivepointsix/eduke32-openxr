#ifndef DUKEVR_OPENXR_H
#define DUKEVR_OPENXR_H

#include <stdint.h>

/* OpenXR world render scale, expressed as a percentage of the desktop-sized
 * intermediate eye target. It is exposed as the vr_render_scale console
 * variable and deliberately does not change the desktop window resolution. */
extern int g_dukeVrOpenXRRenderScale;

/* The OpenXR bridge is deliberately small: BRender continues to own the
 * scene and its eye render targets, while this module owns only the runtime
 * session, frame timing, swapchain images, and submission. */
int DukeVROpenXR_Enabled(void);
int DukeVROpenXR_Initialize(void);
int DukeVROpenXR_PrepareInput(void);
int DukeVROpenXR_BeginFrame(void);
int DukeVROpenXR_FrameActive(void);
int DukeVROpenXR_BeginEyeRender(int eye);
void DukeVROpenXR_EndEyeRender(void);
int DukeVROpenXR_BeginHudRender(void);
void DukeVROpenXR_EndHudRender(void);
#define DUKEVR_OPENXR_HUD_LAYER_WEAPON 1
#define DUKEVR_OPENXR_HUD_LAYER_STATUS 2
int DukeVROpenXR_BeginHudLayer(int layer);
void DukeVROpenXR_EndHudLayer(void);
void DukeVROpenXR_SetHudLayerOffsets(int weapon_x, int weapon_y,
    int status_x, int status_y);
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
int DukeVROpenXR_GetControllerPose(int hand, float position_xyz[3],
    float orientation_xyzw[4], int* grip_pressed);

/* Frame-synchronised controller input.  The values remain available until
 * the next OpenXR action sync, which lets the game input tick consume the
 * state before the following render frame begins. */
typedef struct DukeVROpenXRControllerInput
{
    float thumbstick[2][2];
    float trigger[2];
    int grip[2];
    int primary[2];
    int secondary[2];
    int thumbstick_click[2];
    int menu[2];
} DukeVROpenXRControllerInput;

int DukeVROpenXR_GetControllerInput(DukeVROpenXRControllerInput* input);
void DukeVROpenXR_ApplyGameplayInput(void* control_info);
int DukeVROpenXR_GetMenuInput(int* direction, int* advance, int* back, int* escape);
void DukeVROpenXR_ClearMenuDirection(void);
void DukeVROpenXR_ClearMenuAdvance(void);
void DukeVROpenXR_ClearMenuBack(void);
void DukeVROpenXR_ClearMenuEscape(void);
void DukeVROpenXR_ClearMenuInput(void);
int DukeVROpenXR_MenuStickActive(void);
int DukeVROpenXR_ConsumeSnapTurn(void);
int DukeVROpenXR_ConsumeWeaponChange(void);
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
