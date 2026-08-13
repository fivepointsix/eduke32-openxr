#include "dukevr_openxr.h"

#ifdef POLYMER
#include "polymer.h"
#endif
#include "baselayer.h"
#include "glbuild.h"
#include "control.h"
#include "function.h"

#ifdef OPENXR

#include "OVR_CAPI_GL.h"

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#include <windows.h>
#include <tlhelp32.h>
#include <GL/gl.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct DukeVROVRSwapChain {
    int eye;
    int auxiliary;
    int hud;
    GLuint texture;
} DukeVROVRSwapChain;

#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_SRGB8_ALPHA8
#define GL_SRGB8_ALPHA8 0x8C43
#endif

#define DUKEVR_OPENXR_MAX_IMAGES 16
#define DUKEVR_OPENXR_MOVABLE_LAYERS 2

typedef struct {
    XrInstance instance;
    XrSystemId system_id;
    XrSession session;
    XrSpace space;
    XrSpace view_space;
    XrSessionState session_state;
    XrActionSet action_set;
    XrAction grip_action;
    XrAction pose_action;
    XrAction thumbstick_action;
    XrAction trigger_action;
    XrAction primary_action;
    XrAction secondary_action;
    XrAction thumbstick_click_action;
    XrAction menu_action;
    XrPath hand_paths[2];
    XrSpace hand_spaces[2];
    int actions_created;
    int actions_attached;
    int controller_valid[2];
    int controller_grip[2];
    float controller_thumbstick[2][2];
    float controller_trigger[2];
    int controller_primary[2];
    int controller_secondary[2];
    int controller_thumbstick_click[2];
    int controller_menu[2];
    int previous_primary[2];
    int previous_secondary[2];
    int previous_trigger[2];
    int previous_thumbstick_click[2];
    int previous_menu[2];
    int menu_direction;
    int menu_direction_pending;
    uint64_t menu_direction_next_repeat;
    int menu_advance_pending;
    int menu_back_pending;
    int menu_escape_pending;
    int snap_turn_latched;
    int snap_turn_pending;
    int weapon_stick_latched;
    int weapon_change_pending;
    float controller_position[2][3];
    float controller_orientation[2][4];
    XrViewConfigurationView config_views[2];
    XrView views[2];
    XrSwapchain swapchains[2];
    XrSwapchain hud_swapchain;
    XrSwapchain movable_swapchains[DUKEVR_OPENXR_MOVABLE_LAYERS];
    XrSwapchainImageOpenGLKHR images[2][DUKEVR_OPENXR_MAX_IMAGES];
    XrSwapchainImageOpenGLKHR hud_images[DUKEVR_OPENXR_MAX_IMAGES];
    XrSwapchainImageOpenGLKHR movable_images[DUKEVR_OPENXR_MOVABLE_LAYERS][DUKEVR_OPENXR_MAX_IMAGES];
    uint32_t image_counts[2];
    uint32_t hud_image_count;
    uint32_t movable_image_counts[DUKEVR_OPENXR_MOVABLE_LAYERS];
    XrTime predicted_display_time;
    XrCompositionLayerProjectionView layer_views[2];
    int initialized;
    int graphics_initialized;
    int vdxr_runtime;
    int vdxr_skip_wait;
    int attempted;
    int session_running;
    int frame_active;
    int frame_begun;
    int scene_frame_submitted;
    int mono_frame_submitted;
    int mono_quad_submitted;
    int mono_source_width;
    int mono_source_height;
    uint32_t images_acquired[2];
    int swapchain_acquired[2];
    uint32_t hud_image_acquired;
    int hud_swapchain_acquired;
    int hud_content_submitted;
    uint32_t movable_images_acquired[DUKEVR_OPENXR_MOVABLE_LAYERS];
    int movable_swapchains_acquired[DUKEVR_OPENXR_MOVABLE_LAYERS];
    int movable_content_submitted[DUKEVR_OPENXR_MOVABLE_LAYERS];
    GLuint eye_fbos[2];
    GLuint eye_depth[2];
    GLuint scene_fbos[2];
    GLuint scene_textures[2];
    GLuint scene_depth[2];
    int scene_target_width;
    int scene_target_height;
    GLuint hud_fbo;
    GLuint hud_texture;
    GLuint hud_depth;
    GLuint hud_runtime_fbo;
    int hud_target_width;
    int hud_target_height;
    GLint saved_hud_draw_framebuffer;
    GLint saved_hud_read_framebuffer;
    GLint saved_hud_viewport[4];
    int hud_render_active;
    GLuint movable_fbos[DUKEVR_OPENXR_MOVABLE_LAYERS];
    GLuint movable_textures[DUKEVR_OPENXR_MOVABLE_LAYERS];
    GLuint movable_depth[DUKEVR_OPENXR_MOVABLE_LAYERS];
    GLuint movable_runtime_fbos[DUKEVR_OPENXR_MOVABLE_LAYERS];
    int movable_target_width;
    int movable_target_height;
    int movable_render_active;
    int movable_offsets[DUKEVR_OPENXR_MOVABLE_LAYERS][2];
    int hud_menu_scaled;
    GLint saved_draw_framebuffer;
    GLint saved_read_framebuffer;
    GLint saved_viewport[4];
    int eye_render_active;
    int current_eye;
    /* Fractional head rotation left over after the legacy 2048-step angle
     * conversion. Polymer consumes this in its floating-point view matrix,
     * eliminating visible stepping without changing gameplay math. */
    float render_yaw_residual;
    float render_pitch_residual;
    float render_roll_residual;
    float render_pos_residual[3];
    /* The legacy renderer asks for eye offsets before the first OpenXR frame
     * is available. Keep a conservative fallback, then replace it with the
     * centered offsets reported by xrLocateViews(). */
    float eye_offset_x[2];
    int have_eye_geometry;
} tDukeVROpenXR;

static tDukeVROpenXR gOpenXR;
static int gDukeVROpenXREyeChainCount;

#ifdef OCULUS
extern volatile int32_t oculusUseMenuScaleForHUD;
#endif

static void OpenXRAbortFrame(void);

static void OpenXRReleaseSceneTargets(void) {
    if (gOpenXR.scene_fbos[0] != 0 || gOpenXR.scene_fbos[1] != 0)
        glDeleteFramebuffers(2, gOpenXR.scene_fbos);
    if (gOpenXR.scene_textures[0] != 0 || gOpenXR.scene_textures[1] != 0)
        glDeleteTextures(2, gOpenXR.scene_textures);
    if (gOpenXR.scene_depth[0] != 0 || gOpenXR.scene_depth[1] != 0)
        glDeleteRenderbuffers(2, gOpenXR.scene_depth);
    gOpenXR.scene_fbos[0] = gOpenXR.scene_fbos[1] = 0;
    gOpenXR.scene_textures[0] = gOpenXR.scene_textures[1] = 0;
    gOpenXR.scene_depth[0] = gOpenXR.scene_depth[1] = 0;
    gOpenXR.scene_target_width = 0;
    gOpenXR.scene_target_height = 0;
}

static void OpenXRReleaseHudTarget(void) {
    if (gOpenXR.hud_fbo != 0)
        glDeleteFramebuffers(1, &gOpenXR.hud_fbo);
    if (gOpenXR.hud_runtime_fbo != 0)
        glDeleteFramebuffers(1, &gOpenXR.hud_runtime_fbo);
    if (gOpenXR.hud_texture != 0)
        glDeleteTextures(1, &gOpenXR.hud_texture);
    if (gOpenXR.hud_depth != 0)
        glDeleteRenderbuffers(1, &gOpenXR.hud_depth);
    gOpenXR.hud_fbo = 0;
    gOpenXR.hud_runtime_fbo = 0;
    gOpenXR.hud_texture = 0;
    gOpenXR.hud_depth = 0;
    gOpenXR.hud_target_width = 0;
    gOpenXR.hud_target_height = 0;
    gOpenXR.hud_render_active = 0;
}

static void OpenXRReleaseMovableTargets(void) {
    glDeleteFramebuffers(DUKEVR_OPENXR_MOVABLE_LAYERS, gOpenXR.movable_fbos);
    glDeleteFramebuffers(DUKEVR_OPENXR_MOVABLE_LAYERS, gOpenXR.movable_runtime_fbos);
    glDeleteTextures(DUKEVR_OPENXR_MOVABLE_LAYERS, gOpenXR.movable_textures);
    glDeleteRenderbuffers(DUKEVR_OPENXR_MOVABLE_LAYERS, gOpenXR.movable_depth);
    memset(gOpenXR.movable_fbos, 0, sizeof(gOpenXR.movable_fbos));
    memset(gOpenXR.movable_runtime_fbos, 0, sizeof(gOpenXR.movable_runtime_fbos));
    memset(gOpenXR.movable_textures, 0, sizeof(gOpenXR.movable_textures));
    memset(gOpenXR.movable_depth, 0, sizeof(gOpenXR.movable_depth));
    gOpenXR.movable_target_width = 0;
    gOpenXR.movable_target_height = 0;
    gOpenXR.movable_render_active = 0;
}

static int OpenXRResultOK(XrResult result, const char* operation) {
    if (XR_SUCCEEDED(result))
        return 1;
    initprintf("OpenXR: %s failed (%d)", operation, (int)result);
    return 0;
}

#ifdef POLYMER
static void OpenXRApplyRendererFov(void) {
    float horizontal;
    float vertical;

    if (!gOpenXR.frame_active)
        return;
    horizontal = ((-gOpenXR.views[0].fov.angleLeft + gOpenXR.views[0].fov.angleRight) +
        (-gOpenXR.views[1].fov.angleLeft + gOpenXR.views[1].fov.angleRight)) * 0.5f;
    vertical = ((gOpenXR.views[0].fov.angleUp - gOpenXR.views[0].fov.angleDown) +
        (gOpenXR.views[1].fov.angleUp - gOpenXR.views[1].fov.angleDown)) * 0.5f;
    if (horizontal > 0.f && vertical > 0.f) {
        /* Keep Polymer's sprite/HUD projection in the same FOV family as the
         * runtime projection. The world frustum is installed separately by
         * polymer_drawrooms(), but weapon and menu sprites still use these
         * renderer CVARs. This is the equivalent of the legacy
         * oculus_init_viewaspect() update. */
        pr_customaspect = (double)horizontal / (double)vertical;
        pr_fov = (int32_t)(vertical * 2048.f / (2.f * (float)PI) + 0.5f);
    }
}
#endif

static void OpenXRReset(void) {
    memset(&gOpenXR, 0, sizeof(gOpenXR));
    gDukeVROpenXREyeChainCount = 0;
    gOpenXR.instance = XR_NULL_HANDLE;
    gOpenXR.session = XR_NULL_HANDLE;
    gOpenXR.space = XR_NULL_HANDLE;
    gOpenXR.view_space = XR_NULL_HANDLE;
    gOpenXR.hud_swapchain = XR_NULL_HANDLE;
    for (int i = 0; i < DUKEVR_OPENXR_MOVABLE_LAYERS; ++i)
        gOpenXR.movable_swapchains[i] = XR_NULL_HANDLE;
    gOpenXR.session_state = XR_SESSION_STATE_UNKNOWN;
    gOpenXR.current_eye = -1;
    gOpenXR.eye_offset_x[0] = -0.032f;
    gOpenXR.eye_offset_x[1] =  0.032f;
}

/* Initialization can fail at any stage (for example when SteamVR is not the
 * active runtime). Always destroy partially-created objects before falling
 * back to the desktop path, otherwise retrying once per game frame can hit
 * the runtime's instance limit. */
static void OpenXRReleaseGraphics(void) {
    uint32_t i;
    if (gOpenXR.frame_begun || gOpenXR.frame_active)
        OpenXRAbortFrame();

    /* Swapchains and spaces must be released after the session has stopped.
     * In normal play the runtime sends STOPPING asynchronously, but quitting
     * can race that event.  Explicitly end a running session here so the
     * runtime cannot wait on a frame that will never be submitted again. */
    if (gOpenXR.session != XR_NULL_HANDLE && gOpenXR.session_running)
    {
        initprintf("OpenXR: ending session during shutdown...");
        if (OpenXRResultOK(xrEndSession(gOpenXR.session), "xrEndSession(shutdown)"))
            gOpenXR.session_running = 0;
    }

    initprintf("OpenXR: releasing graphics resources...");
    OpenXRReleaseSceneTargets();
    OpenXRReleaseHudTarget();
    OpenXRReleaseMovableTargets();
    if (gOpenXR.eye_fbos[0] != 0 || gOpenXR.eye_fbos[1] != 0)
        glDeleteFramebuffers(2, gOpenXR.eye_fbos);
    if (gOpenXR.eye_depth[0] != 0 || gOpenXR.eye_depth[1] != 0)
        glDeleteRenderbuffers(2, gOpenXR.eye_depth);
    gOpenXR.eye_fbos[0] = gOpenXR.eye_fbos[1] = 0;
    gOpenXR.eye_depth[0] = gOpenXR.eye_depth[1] = 0;
    for (i = 0; i < 2; i++) {
        if (gOpenXR.swapchains[i] != XR_NULL_HANDLE)
            xrDestroySwapchain(gOpenXR.swapchains[i]);
    }
    if (gOpenXR.hud_swapchain != XR_NULL_HANDLE)
        xrDestroySwapchain(gOpenXR.hud_swapchain);
    gOpenXR.hud_swapchain = XR_NULL_HANDLE;
    for (i = 0; i < DUKEVR_OPENXR_MOVABLE_LAYERS; ++i) {
        if (gOpenXR.movable_swapchains[i] != XR_NULL_HANDLE)
            xrDestroySwapchain(gOpenXR.movable_swapchains[i]);
        gOpenXR.movable_swapchains[i] = XR_NULL_HANDLE;
    }
    if (gOpenXR.view_space != XR_NULL_HANDLE)
        xrDestroySpace(gOpenXR.view_space);
    gOpenXR.view_space = XR_NULL_HANDLE;
    for (i = 0; i < 2; i++) {
        if (gOpenXR.hand_spaces[i] != XR_NULL_HANDLE)
            xrDestroySpace(gOpenXR.hand_spaces[i]);
        gOpenXR.hand_spaces[i] = XR_NULL_HANDLE;
    }
    if (gOpenXR.space != XR_NULL_HANDLE)
        xrDestroySpace(gOpenXR.space);
    if (gOpenXR.session != XR_NULL_HANDLE)
        xrDestroySession(gOpenXR.session);
    gOpenXR.swapchains[0] = XR_NULL_HANDLE;
    gOpenXR.swapchains[1] = XR_NULL_HANDLE;
    gOpenXR.space = XR_NULL_HANDLE;
    gOpenXR.view_space = XR_NULL_HANDLE;
    gOpenXR.session = XR_NULL_HANDLE;
    gOpenXR.session_running = 0;
    gOpenXR.graphics_initialized = 0;
}

static void OpenXRReleaseActions(void) {
    if (gOpenXR.menu_action != XR_NULL_HANDLE)
        xrDestroyAction(gOpenXR.menu_action);
    if (gOpenXR.thumbstick_click_action != XR_NULL_HANDLE)
        xrDestroyAction(gOpenXR.thumbstick_click_action);
    if (gOpenXR.secondary_action != XR_NULL_HANDLE)
        xrDestroyAction(gOpenXR.secondary_action);
    if (gOpenXR.primary_action != XR_NULL_HANDLE)
        xrDestroyAction(gOpenXR.primary_action);
    if (gOpenXR.trigger_action != XR_NULL_HANDLE)
        xrDestroyAction(gOpenXR.trigger_action);
    if (gOpenXR.thumbstick_action != XR_NULL_HANDLE)
        xrDestroyAction(gOpenXR.thumbstick_action);
    if (gOpenXR.pose_action != XR_NULL_HANDLE)
        xrDestroyAction(gOpenXR.pose_action);
    if (gOpenXR.grip_action != XR_NULL_HANDLE)
        xrDestroyAction(gOpenXR.grip_action);
    if (gOpenXR.action_set != XR_NULL_HANDLE)
        xrDestroyActionSet(gOpenXR.action_set);
    gOpenXR.pose_action = XR_NULL_HANDLE;
    gOpenXR.grip_action = XR_NULL_HANDLE;
    gOpenXR.thumbstick_action = XR_NULL_HANDLE;
    gOpenXR.trigger_action = XR_NULL_HANDLE;
    gOpenXR.primary_action = XR_NULL_HANDLE;
    gOpenXR.secondary_action = XR_NULL_HANDLE;
    gOpenXR.thumbstick_click_action = XR_NULL_HANDLE;
    gOpenXR.menu_action = XR_NULL_HANDLE;
    gOpenXR.action_set = XR_NULL_HANDLE;
    gOpenXR.actions_created = 0;
    gOpenXR.actions_attached = 0;
}

static void OpenXRReleaseResources(void) {
    OpenXRReleaseGraphics();
    OpenXRReleaseActions();
    if (gOpenXR.instance != XR_NULL_HANDLE)
        xrDestroyInstance(gOpenXR.instance);
    OpenXRReset();
}

static int OpenXRInitializationFailed(void) {
    OpenXRReleaseResources();
    gOpenXR.attempted = 1;
    return 0;
}

int DukeVROpenXR_Enabled(void) {
    const char *disabled = getenv("DUKEVR_OPENXR_DISABLE");
    return disabled == NULL || strcmp(disabled, "1") != 0;
}

static int OpenXRExtensionAvailable(const char* requested) {
    uint32_t count = 0;
    uint32_t i;
    XrExtensionProperties* properties;
    XrResult result;

    result = xrEnumerateInstanceExtensionProperties(NULL, 0, &count, NULL);
    if (!OpenXRResultOK(result, "xrEnumerateInstanceExtensionProperties(count)"))
        return 0;
    properties = (XrExtensionProperties*)calloc(count, sizeof(*properties));
    if (properties == NULL)
        return 0;
    for (i = 0; i < count; i++)
        properties[i].type = XR_TYPE_EXTENSION_PROPERTIES;
    result = xrEnumerateInstanceExtensionProperties(NULL, count, &count, properties);
    if (!OpenXRResultOK(result, "xrEnumerateInstanceExtensionProperties")) {
        free(properties);
        return 0;
    }
    for (i = 0; i < count; i++) {
        if (strcmp(properties[i].extensionName, requested) == 0) {
            free(properties);
            return 1;
        }
    }
    free(properties);
    return 0;
}

/* SteamVR's OpenXR runtime can block inside xrCreateInstance while its
 * vrserver process is still starting (or belongs to another Windows token).
 * The menu path is called during ordinary desktop rendering, so never make
 * that path wait on a runtime that is not ready.  Once the namespace pipe is
 * present, normal OpenXR initialization is safe to attempt. */
static int OpenXRRuntimeReady(void) {
    const char* runtime_json = getenv("XR_RUNTIME_JSON");
    HANDLE snapshot;
    PROCESSENTRY32 entry;
    int server_accessible = 0;

    if (runtime_json == NULL || strstr(runtime_json, "steamxr_win64.json") == NULL)
        return 1;

    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;
    memset(&entry, 0, sizeof(entry));
    entry.dwSize = sizeof(entry);
    if (Process32First(snapshot, &entry)) {
        do {
            if (_stricmp(entry.szExeFile, "vrserver.exe") == 0) {
                HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
                if (process != NULL) {
                    server_accessible = 1;
                    CloseHandle(process);
                    break;
                }
            }
        } while (Process32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    if (!server_accessible)
        return 0;

    return WaitNamedPipeA("\\\\.\\pipe\\SteamVR_Namespace", 0) != FALSE;
}

static int OpenXRCreateActions(void) {
    static const char* hand_names[2] = { "/user/hand/left", "/user/hand/right" };
    static const char* profiles[] = {
        "/interaction_profiles/oculus/touch_controller",
        "/interaction_profiles/valve/index_controller",
        "/interaction_profiles/microsoft/motion_controller",
    };
    XrActionSetCreateInfo set_info;
    XrActionCreateInfo action_info;
    uint32_t i;

    if (gOpenXR.instance == XR_NULL_HANDLE)
        return 0;

    for (i = 0; i < 2; i++) {
        if (!OpenXRResultOK(xrStringToPath(gOpenXR.instance, hand_names[i], &gOpenXR.hand_paths[i]),
                "xrStringToPath(hand)"))
            return 0;
    }

    memset(&set_info, 0, sizeof(set_info));
    set_info.type = XR_TYPE_ACTION_SET_CREATE_INFO;
    strcpy(set_info.actionSetName, "dukevr");
    strcpy(set_info.localizedActionSetName, "DukeVR Controls");
    set_info.priority = 0;
    if (!OpenXRResultOK(xrCreateActionSet(gOpenXR.instance, &set_info, &gOpenXR.action_set),
            "xrCreateActionSet"))
        return 0;

    memset(&action_info, 0, sizeof(action_info));
    action_info.type = XR_TYPE_ACTION_CREATE_INFO;
    strcpy(action_info.actionName, "grab_hud");
    strcpy(action_info.localizedActionName, "Grab HUD");
    action_info.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
    action_info.countSubactionPaths = 2;
    action_info.subactionPaths = gOpenXR.hand_paths;
    if (!OpenXRResultOK(xrCreateAction(gOpenXR.action_set, &action_info, &gOpenXR.grip_action),
            "xrCreateAction(grip)")) {
        OpenXRReleaseActions();
        return 0;
    }

    memset(&action_info, 0, sizeof(action_info));
    action_info.type = XR_TYPE_ACTION_CREATE_INFO;
    strcpy(action_info.actionName, "hud_pose");
    strcpy(action_info.localizedActionName, "HUD Controller Pose");
    action_info.actionType = XR_ACTION_TYPE_POSE_INPUT;
    action_info.countSubactionPaths = 2;
    action_info.subactionPaths = gOpenXR.hand_paths;
    if (!OpenXRResultOK(xrCreateAction(gOpenXR.action_set, &action_info, &gOpenXR.pose_action),
            "xrCreateAction(pose)")) {
        OpenXRReleaseActions();
        return 0;
    }

    struct ActionDescription {
        const char* name;
        const char* localized_name;
        XrActionType type;
        XrAction* action;
        const char* error_name;
    } const action_descriptions[] = {
        { "thumbstick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT,
          &gOpenXR.thumbstick_action, "xrCreateAction(thumbstick)" },
        { "trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT,
          &gOpenXR.trigger_action, "xrCreateAction(trigger)" },
        { "primary", "Primary Button", XR_ACTION_TYPE_BOOLEAN_INPUT,
          &gOpenXR.primary_action, "xrCreateAction(primary)" },
        { "secondary", "Secondary Button", XR_ACTION_TYPE_BOOLEAN_INPUT,
          &gOpenXR.secondary_action, "xrCreateAction(secondary)" },
        { "thumbstick_click", "Thumbstick Click", XR_ACTION_TYPE_BOOLEAN_INPUT,
          &gOpenXR.thumbstick_click_action, "xrCreateAction(thumbstick click)" },
        { "menu", "Menu Button", XR_ACTION_TYPE_BOOLEAN_INPUT,
          &gOpenXR.menu_action, "xrCreateAction(menu)" },
    };
    for (auto const& description : action_descriptions) {
        memset(&action_info, 0, sizeof(action_info));
        action_info.type = XR_TYPE_ACTION_CREATE_INFO;
        strcpy(action_info.actionName, description.name);
        strcpy(action_info.localizedActionName, description.localized_name);
        action_info.actionType = description.type;
        action_info.countSubactionPaths = 2;
        action_info.subactionPaths = gOpenXR.hand_paths;
        if (!OpenXRResultOK(xrCreateAction(gOpenXR.action_set, &action_info, description.action),
                description.error_name)) {
            OpenXRReleaseActions();
            return 0;
        }
    }

    for (i = 0; i < ARRAY_SIZE(profiles); i++) {
        const char* grip_paths[2] = {
            "/user/hand/left/input/squeeze/value",
            "/user/hand/right/input/squeeze/value",
        };
        const char* pose_paths[2] = {
            "/user/hand/left/input/grip/pose",
            "/user/hand/right/input/grip/pose",
        };
        XrPath profile_path;
        const char* thumbstick_paths[2] = {
            "/user/hand/left/input/thumbstick",
            "/user/hand/right/input/thumbstick",
        };
        const char* trigger_paths[2] = {
            "/user/hand/left/input/trigger/value",
            "/user/hand/right/input/trigger/value",
        };
        const char* primary_paths[2] = {
            i == 1 ? "/user/hand/left/input/a/click" : "/user/hand/left/input/x/click",
            "/user/hand/right/input/a/click",
        };
        const char* secondary_paths[2] = {
            i == 1 ? "/user/hand/left/input/b/click" : "/user/hand/left/input/y/click",
            "/user/hand/right/input/b/click",
        };
        const char* thumbstick_click_paths[2] = {
            "/user/hand/left/input/thumbstick/click",
            "/user/hand/right/input/thumbstick/click",
        };
        const char* menu_paths[2] = {
            i == 1 ? "/user/hand/left/input/application_menu/click" : "/user/hand/left/input/menu/click",
            i == 1 ? "/user/hand/right/input/application_menu/click" : "/user/hand/right/input/menu/click",
        };
        XrPath paths[24];
        XrActionSuggestedBinding bindings[32];
        XrInteractionProfileSuggestedBinding suggested;
        uint32_t binding_count = 0;

        if (!OpenXRResultOK(xrStringToPath(gOpenXR.instance, profiles[i], &profile_path),
                "xrStringToPath(profile)"))
            continue;
#define DUKEVR_ADD_BINDING(action_handle, path_string) do { \
            if (!OpenXRResultOK(xrStringToPath(gOpenXR.instance, path_string, &paths[binding_count]), \
                    "xrStringToPath(controller binding)")) \
                continue; \
            bindings[binding_count].action = action_handle; \
            bindings[binding_count].binding = paths[binding_count]; \
            binding_count++; \
        } while (0)
        const int motion_controller = i == 2;
        for (uint32_t hand = 0; hand < 2; hand++) {
            DUKEVR_ADD_BINDING(gOpenXR.grip_action, grip_paths[hand]);
            DUKEVR_ADD_BINDING(gOpenXR.pose_action, pose_paths[hand]);
            DUKEVR_ADD_BINDING(gOpenXR.thumbstick_action, motion_controller
                ? (hand == 0 ? "/user/hand/left/input/trackpad" : "/user/hand/right/input/trackpad")
                : thumbstick_paths[hand]);
            DUKEVR_ADD_BINDING(gOpenXR.trigger_action, trigger_paths[hand]);
            if (motion_controller)
                DUKEVR_ADD_BINDING(gOpenXR.primary_action, hand == 0
                    ? "/user/hand/left/input/trackpad/click"
                    : "/user/hand/right/input/trackpad/click");
            else {
                DUKEVR_ADD_BINDING(gOpenXR.primary_action, primary_paths[hand]);
                DUKEVR_ADD_BINDING(gOpenXR.secondary_action, secondary_paths[hand]);
            }
            if (!motion_controller)
                DUKEVR_ADD_BINDING(gOpenXR.thumbstick_click_action, thumbstick_click_paths[hand]);
            /* The left controller owns the menu action on Touch/Index-style
             * controllers. The right-side suggestion is useful for devices
             * that expose an application/menu button on both hands. */
            if (hand == 0 || i == 2)
                DUKEVR_ADD_BINDING(gOpenXR.menu_action, menu_paths[hand]);
        }
#undef DUKEVR_ADD_BINDING

        memset(&suggested, 0, sizeof(suggested));
        suggested.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
        suggested.interactionProfile = profile_path;
        suggested.countSuggestedBindings = binding_count;
        suggested.suggestedBindings = bindings;
        if (!OpenXRResultOK(xrSuggestInteractionProfileBindings(gOpenXR.instance, &suggested),
                "xrSuggestInteractionProfileBindings"))
            initprintf("OpenXR: controller profile suggestion rejected: %s", profiles[i]);
    }

    gOpenXR.actions_created = 1;
    initprintf("OpenXR: controller actions created (movement, snap turn, weapons, gameplay, menus)");
    return 1;
}

static void OpenXRSyncControllerActions(void) {
    XrActiveActionSet active_set;
    XrActionsSyncInfo sync_info;
    uint32_t hand;

    uint64_t const now = GetTickCount64();
    int old_direction = gOpenXR.menu_direction;
    int new_direction = 0;

    memset(gOpenXR.controller_valid, 0, sizeof(gOpenXR.controller_valid));
    memset(gOpenXR.controller_grip, 0, sizeof(gOpenXR.controller_grip));
    memset(gOpenXR.controller_thumbstick, 0, sizeof(gOpenXR.controller_thumbstick));
    memset(gOpenXR.controller_trigger, 0, sizeof(gOpenXR.controller_trigger));
    memset(gOpenXR.controller_primary, 0, sizeof(gOpenXR.controller_primary));
    memset(gOpenXR.controller_secondary, 0, sizeof(gOpenXR.controller_secondary));
    memset(gOpenXR.controller_thumbstick_click, 0, sizeof(gOpenXR.controller_thumbstick_click));
    memset(gOpenXR.controller_menu, 0, sizeof(gOpenXR.controller_menu));
    if (!gOpenXR.actions_created || !gOpenXR.actions_attached ||
        gOpenXR.session == XR_NULL_HANDLE || gOpenXR.view_space == XR_NULL_HANDLE)
        return;

    memset(&active_set, 0, sizeof(active_set));
    active_set.actionSet = gOpenXR.action_set;
    active_set.subactionPath = XR_NULL_PATH;
    memset(&sync_info, 0, sizeof(sync_info));
    sync_info.type = XR_TYPE_ACTIONS_SYNC_INFO;
    sync_info.countActiveActionSets = 1;
    sync_info.activeActionSets = &active_set;
    if (!OpenXRResultOK(xrSyncActions(gOpenXR.session, &sync_info), "xrSyncActions"))
        return;

    for (hand = 0; hand < 2; hand++) {
        XrActionStateGetInfo get_info;
        XrActionStateFloat grip_state;
        XrActionStateFloat trigger_state;
        XrActionStateVector2f thumbstick_state;
        XrActionStateBoolean primary_state;
        XrActionStateBoolean secondary_state;
        XrActionStateBoolean thumbstick_click_state;
        XrActionStateBoolean menu_state;
        XrSpaceLocation location;
        memset(&get_info, 0, sizeof(get_info));
        get_info.type = XR_TYPE_ACTION_STATE_GET_INFO;
        get_info.action = gOpenXR.grip_action;
        get_info.subactionPath = gOpenXR.hand_paths[hand];
        memset(&grip_state, 0, sizeof(grip_state));
        grip_state.type = XR_TYPE_ACTION_STATE_FLOAT;
        if (!OpenXRResultOK(xrGetActionStateFloat(gOpenXR.session, &get_info, &grip_state),
                "xrGetActionStateFloat"))
            continue;

#define DUKEVR_GET_ACTION_STATE(action_handle, state, state_type, getter, label) do { \
            memset(&(state), 0, sizeof(state)); \
            (state).type = state_type; \
            get_info.action = action_handle; \
            if (!OpenXRResultOK(getter(gOpenXR.session, &get_info, &(state)), label)) \
                continue; \
        } while (0)
        DUKEVR_GET_ACTION_STATE(gOpenXR.trigger_action, trigger_state,
            XR_TYPE_ACTION_STATE_FLOAT, xrGetActionStateFloat, "xrGetActionStateFloat(trigger)");
        DUKEVR_GET_ACTION_STATE(gOpenXR.thumbstick_action, thumbstick_state,
            XR_TYPE_ACTION_STATE_VECTOR2F, xrGetActionStateVector2f, "xrGetActionStateVector2f");
        DUKEVR_GET_ACTION_STATE(gOpenXR.primary_action, primary_state,
            XR_TYPE_ACTION_STATE_BOOLEAN, xrGetActionStateBoolean, "xrGetActionStateBoolean(primary)");
        DUKEVR_GET_ACTION_STATE(gOpenXR.secondary_action, secondary_state,
            XR_TYPE_ACTION_STATE_BOOLEAN, xrGetActionStateBoolean, "xrGetActionStateBoolean(secondary)");
        DUKEVR_GET_ACTION_STATE(gOpenXR.thumbstick_click_action, thumbstick_click_state,
            XR_TYPE_ACTION_STATE_BOOLEAN, xrGetActionStateBoolean, "xrGetActionStateBoolean(stick click)");
        DUKEVR_GET_ACTION_STATE(gOpenXR.menu_action, menu_state,
            XR_TYPE_ACTION_STATE_BOOLEAN, xrGetActionStateBoolean, "xrGetActionStateBoolean(menu)");
#undef DUKEVR_GET_ACTION_STATE

        gOpenXR.controller_thumbstick[hand][0] = thumbstick_state.isActive ? thumbstick_state.currentState.x : 0.f;
        gOpenXR.controller_thumbstick[hand][1] = thumbstick_state.isActive ? thumbstick_state.currentState.y : 0.f;
        gOpenXR.controller_trigger[hand] = trigger_state.isActive ? trigger_state.currentState : 0.f;
        gOpenXR.controller_grip[hand] = grip_state.isActive && grip_state.currentState > 0.55f;
        gOpenXR.controller_primary[hand] = primary_state.isActive && primary_state.currentState;
        gOpenXR.controller_secondary[hand] = secondary_state.isActive && secondary_state.currentState;
        gOpenXR.controller_thumbstick_click[hand] = thumbstick_click_state.isActive && thumbstick_click_state.currentState;
        gOpenXR.controller_menu[hand] = menu_state.isActive && menu_state.currentState;
        if (hand == 0 && !gOpenXR.controller_grip[hand] &&
            gOpenXR.controller_primary[hand] && !gOpenXR.previous_primary[hand])
            gOpenXR.weapon_change_pending = -1;
        if (hand == 0 && !gOpenXR.controller_grip[hand] &&
            gOpenXR.controller_secondary[hand] && !gOpenXR.previous_secondary[hand])
            gOpenXR.weapon_change_pending = 1;
        if (hand == 1 && gOpenXR.controller_primary[hand] && !gOpenXR.previous_primary[hand])
            gOpenXR.menu_advance_pending = 1;
        if (hand == 1 && gOpenXR.controller_secondary[hand] && !gOpenXR.previous_secondary[hand])
            gOpenXR.menu_back_pending = 1;
        if (hand == 1 && trigger_state.isActive && trigger_state.currentState > 0.55f &&
            !gOpenXR.previous_trigger[hand])
            gOpenXR.menu_advance_pending = 1;
        if (gOpenXR.controller_menu[hand] && !gOpenXR.previous_menu[hand])
            gOpenXR.menu_escape_pending = 1;
        gOpenXR.previous_primary[hand] = gOpenXR.controller_primary[hand];
        gOpenXR.previous_secondary[hand] = gOpenXR.controller_secondary[hand];
        gOpenXR.previous_trigger[hand] = trigger_state.isActive && trigger_state.currentState > 0.55f;
        gOpenXR.previous_thumbstick_click[hand] = gOpenXR.controller_thumbstick_click[hand];
        gOpenXR.previous_menu[hand] = gOpenXR.controller_menu[hand];

        memset(&location, 0, sizeof(location));
        location.type = XR_TYPE_SPACE_LOCATION;
        if (!OpenXRResultOK(xrLocateSpace(gOpenXR.hand_spaces[hand], gOpenXR.view_space,
                gOpenXR.predicted_display_time, &location), "xrLocateSpace(controller)"))
            continue;
        if ((location.locationFlags & (XR_SPACE_LOCATION_POSITION_VALID_BIT |
                XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) !=
            (XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
            continue;

        gOpenXR.controller_valid[hand] = 1;
        gOpenXR.controller_position[hand][0] = location.pose.position.x;
        gOpenXR.controller_position[hand][1] = location.pose.position.y;
        gOpenXR.controller_position[hand][2] = location.pose.position.z;
        gOpenXR.controller_orientation[hand][0] = location.pose.orientation.x;
        gOpenXR.controller_orientation[hand][1] = location.pose.orientation.y;
        gOpenXR.controller_orientation[hand][2] = location.pose.orientation.z;
        gOpenXR.controller_orientation[hand][3] = location.pose.orientation.w;
    }

    float const menu_x = gOpenXR.controller_thumbstick[0][0];
    float const menu_y = gOpenXR.controller_thumbstick[0][1];
    if (fabsf(menu_y) > fabsf(menu_x) && fabsf(menu_y) >= .35f)
        new_direction = menu_y > 0.f ? 1 : 2;
    else if (fabsf(menu_x) >= .35f)
        new_direction = menu_x < 0.f ? 3 : 4;
    gOpenXR.menu_direction = new_direction;
    if (new_direction != old_direction) {
        if (new_direction != 0) {
            gOpenXR.menu_direction_pending = 1;
            gOpenXR.menu_direction_next_repeat = now + 500;
        } else {
            gOpenXR.menu_direction_pending = 0;
            gOpenXR.menu_direction_next_repeat = 0;
        }
    } else if (new_direction != 0 && now >= gOpenXR.menu_direction_next_repeat) {
        gOpenXR.menu_direction_pending = 1;
        gOpenXR.menu_direction_next_repeat = now + 60;
    }

    float const turn_x = gOpenXR.controller_thumbstick[1][0];
    if (fabsf(turn_x) >= .65f) {
        if (!gOpenXR.snap_turn_latched) {
            gOpenXR.snap_turn_pending = turn_x < 0.f ? -1 : 1;
            gOpenXR.snap_turn_latched = 1;
        }
    } else
        gOpenXR.snap_turn_latched = 0;

    float const weapon_y = gOpenXR.controller_thumbstick[1][1];
    if (fabsf(weapon_y) >= .65f) {
        if (!gOpenXR.weapon_stick_latched) {
            gOpenXR.weapon_change_pending = weapon_y > 0.f ? 1 : -1;
            gOpenXR.weapon_stick_latched = 1;
        }
    } else
        gOpenXR.weapon_stick_latched = 0;
}

int DukeVROpenXR_GetControllerInput(DukeVROpenXRControllerInput* input) {
    if (input == NULL || !gOpenXR.initialized)
        return 0;
    for (int hand = 0; hand < 2; ++hand) {
        input->thumbstick[hand][0] = gOpenXR.controller_thumbstick[hand][0];
        input->thumbstick[hand][1] = gOpenXR.controller_thumbstick[hand][1];
        input->trigger[hand] = gOpenXR.controller_trigger[hand];
        input->grip[hand] = gOpenXR.controller_grip[hand];
        input->primary[hand] = gOpenXR.controller_primary[hand];
        input->secondary[hand] = gOpenXR.controller_secondary[hand];
        input->thumbstick_click[hand] = gOpenXR.controller_thumbstick_click[hand];
        input->menu[hand] = gOpenXR.controller_menu[hand];
    }
    return 1;
}

int DukeVROpenXR_GetMenuInput(int* direction, int* advance, int* back, int* escape) {
    if (direction != NULL)
        *direction = gOpenXR.menu_direction_pending ? gOpenXR.menu_direction : 0;
    if (advance != NULL)
        *advance = gOpenXR.menu_advance_pending;
    if (back != NULL)
        *back = gOpenXR.menu_back_pending;
    if (escape != NULL)
        *escape = gOpenXR.menu_escape_pending;
    return gOpenXR.initialized;
}

void DukeVROpenXR_ClearMenuDirection(void) { gOpenXR.menu_direction_pending = 0; }
void DukeVROpenXR_ClearMenuAdvance(void) { gOpenXR.menu_advance_pending = 0; }
void DukeVROpenXR_ClearMenuBack(void) { gOpenXR.menu_back_pending = 0; }
void DukeVROpenXR_ClearMenuEscape(void) { gOpenXR.menu_escape_pending = 0; }
void DukeVROpenXR_ClearMenuInput(void) {
    gOpenXR.menu_direction_pending = 0;
    gOpenXR.menu_advance_pending = 0;
    gOpenXR.menu_back_pending = 0;
    gOpenXR.menu_escape_pending = 0;
}
int DukeVROpenXR_MenuStickActive(void) { return gOpenXR.menu_direction != 0; }

int DukeVROpenXR_ConsumeSnapTurn(void) {
    int const turn = gOpenXR.snap_turn_pending;
    gOpenXR.snap_turn_pending = 0;
    return turn;
}

int DukeVROpenXR_ConsumeWeaponChange(void) {
    int const change = gOpenXR.weapon_change_pending;
    gOpenXR.weapon_change_pending = 0;
    return change;
}

void DukeVROpenXR_ApplyGameplayInput(void* control_info) {
    ControlInfo* const info = (ControlInfo*)control_info;
    DukeVROpenXRControllerInput input;
    if (info == NULL || !DukeVROpenXR_GetControllerInput(&input))
        return;

    /* These edges are menu-only actions. A gameplay input tick may see them
     * while the menu is being opened, so consume them before they can leak
     * into the next menu frame. */
    DukeVROpenXR_ClearMenuAdvance();
    DukeVROpenXR_ClearMenuBack();

    /* OpenXR uses a normalized [-1,1] vector. ControlInfo uses the same
     * 32767 full-scale convention as the existing joystick path. The signs
     * deliberately match EDuke32's modern gamepad mapping. */
    info->dx += (int32_t)lrintf(input.thumbstick[0][0] * 32767.f);
    info->dz -= (int32_t)lrintf(input.thumbstick[0][1] * 32767.f);

#define DUKEVR_SET_BUTTON(function) (CONTROL_ButtonState |= (1ULL << (function)))
    if (input.thumbstick_click[0])
        DUKEVR_SET_BUTTON(gamefunc_Inventory);
    if (input.grip[0])
        DUKEVR_SET_BUTTON(gamefunc_Run);
    if (input.trigger[0] > .55f)
        DUKEVR_SET_BUTTON(gamefunc_Open);
    if (input.grip[0]) {
        if (input.primary[0])
            DUKEVR_SET_BUTTON(gamefunc_Inventory_Left);
        if (input.secondary[0])
            DUKEVR_SET_BUTTON(gamefunc_Inventory_Right);
    }
    if (input.trigger[1] > .55f)
        DUKEVR_SET_BUTTON(gamefunc_Fire);
    if (input.primary[1])
        DUKEVR_SET_BUTTON(gamefunc_Jump);
    if (input.secondary[1])
        DUKEVR_SET_BUTTON(gamefunc_Crouch);
    if (input.grip[1])
        DUKEVR_SET_BUTTON(gamefunc_Quick_Kick);
    int const weapon_change = DukeVROpenXR_ConsumeWeaponChange();
    if (weapon_change > 0)
        DUKEVR_SET_BUTTON(gamefunc_Next_Weapon);
    else if (weapon_change < 0)
        DUKEVR_SET_BUTTON(gamefunc_Previous_Weapon);
#undef DUKEVR_SET_BUTTON
}

static void OpenXRPollEvents(void) {
    XrEventDataBuffer event;

    for (;;) {
        XrResult result;
        memset(&event, 0, sizeof(event));
        event.type = XR_TYPE_EVENT_DATA_BUFFER;
        result = xrPollEvent(gOpenXR.instance, &event);
        if (result == XR_EVENT_UNAVAILABLE)
            return;
        if (!OpenXRResultOK(result, "xrPollEvent"))
            return;

        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            XrEventDataSessionStateChanged* changed = (XrEventDataSessionStateChanged*)&event;
            XrSessionState state = changed->state;
            gOpenXR.session_state = state;
            if (state == XR_SESSION_STATE_READY && !gOpenXR.session_running) {
                XrSessionBeginInfo begin_info;
                memset(&begin_info, 0, sizeof(begin_info));
                begin_info.type = XR_TYPE_SESSION_BEGIN_INFO;
                begin_info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                if (OpenXRResultOK(xrBeginSession(gOpenXR.session, &begin_info), "xrBeginSession"))
                    gOpenXR.session_running = 1;
            } else if (state == XR_SESSION_STATE_STOPPING && gOpenXR.session_running) {
                if (OpenXRResultOK(xrEndSession(gOpenXR.session), "xrEndSession"))
                    gOpenXR.session_running = 0;
            } else if (state == XR_SESSION_STATE_EXITING || state == XR_SESSION_STATE_LOSS_PENDING) {
                gOpenXR.session_running = 0;
            }
        }
    }
}

static int OpenXRCreateSwapchain(int eye) {
    XrSwapchainCreateInfo create_info;
    uint32_t format_count = 0;
    int64_t* formats;
    uint32_t i;
    int64_t chosen_format = 0;
    XrResult result;
    XrResult swapchain_result = XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;
    uint32_t image_count = 0;

    result = xrEnumerateSwapchainFormats(gOpenXR.session, 0, &format_count, NULL);
    if (!OpenXRResultOK(result, "xrEnumerateSwapchainFormats(count)"))
        return 0;
    formats = (int64_t*)calloc(format_count, sizeof(*formats));
    if (formats == NULL)
        return 0;
    result = xrEnumerateSwapchainFormats(gOpenXR.session, format_count, &format_count, formats);
    if (!OpenXRResultOK(result, "xrEnumerateSwapchainFormats")) {
        free(formats);
        return 0;
    }
    if (format_count == 0) {
        free(formats);
        return 0;
    }
    chosen_format = formats[0];
    for (i = 0; i < format_count; i++) {
        if (formats[i] == GL_SRGB8_ALPHA8) {
            chosen_format = formats[i];
            break;
        }
    }
    memset(&create_info, 0, sizeof(create_info));
    create_info.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
    create_info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    create_info.sampleCount = 1;
    create_info.width = gOpenXR.config_views[eye].recommendedImageRectWidth;
    create_info.height = gOpenXR.config_views[eye].recommendedImageRectHeight;
    create_info.faceCount = 1;
    create_info.arraySize = 1;
    create_info.mipCount = 1;
    /* Prefer sRGB, but fall back through every format advertised by the
     * runtime. This handles runtimes that reject one advertised format for
     * the requested usage flags. */
    for (i = 0; i < format_count; i++) {
        uint32_t index = (i == 0 && chosen_format == GL_SRGB8_ALPHA8) ? 0 : i;
        if (i == 0 && chosen_format == GL_SRGB8_ALPHA8) {
            for (index = 0; index < format_count && formats[index] != GL_SRGB8_ALPHA8; index++) {}
            if (index >= format_count)
                continue;
        } else if (formats[i] == GL_SRGB8_ALPHA8) {
            continue;
        }
        create_info.format = formats[index];
        result = xrCreateSwapchain(gOpenXR.session, &create_info, &gOpenXR.swapchains[eye]);
        if (XR_SUCCEEDED(result)) {
            swapchain_result = result;
            chosen_format = create_info.format;
            break;
        }
        initprintf("OpenXR: xrCreateSwapchain format %lld failed (%d)",
            (long long)create_info.format, (int)result);
    }
    free(formats);
    if (!OpenXRResultOK(swapchain_result, "xrCreateSwapchain"))
        return 0;

    result = xrEnumerateSwapchainImages(gOpenXR.swapchains[eye], 0, &image_count, NULL);
    if (!OpenXRResultOK(result, "xrEnumerateSwapchainImages(count)"))
        return 0;
    if (image_count > DUKEVR_OPENXR_MAX_IMAGES)
        image_count = DUKEVR_OPENXR_MAX_IMAGES;
    gOpenXR.image_counts[eye] = image_count;
    for (i = 0; i < image_count; i++)
        gOpenXR.images[eye][i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
    result = xrEnumerateSwapchainImages(gOpenXR.swapchains[eye], image_count, &image_count,
        (XrSwapchainImageBaseHeader*)gOpenXR.images[eye]);
    return OpenXRResultOK(result, "xrEnumerateSwapchainImages");
}

/* The HUD is rendered once into a transparent texture and submitted as a
 * head-locked quad layer. It must be a sampled swapchain (the eye swapchains
 * are color-attachment-only projection targets). */
static int OpenXRCreateHudSwapchain(void) {
    XrSwapchainCreateInfo create_info;
    uint32_t format_count = 0;
    int64_t* formats;
    uint32_t i;
    int64_t chosen_format = 0;
    XrResult result;
    XrResult swapchain_result = XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;
    uint32_t image_count = 0;

    result = xrEnumerateSwapchainFormats(gOpenXR.session, 0, &format_count, NULL);
    if (!OpenXRResultOK(result, "xrEnumerateSwapchainFormats(HUD count)"))
        return 0;
    formats = (int64_t*)calloc(format_count, sizeof(*formats));
    if (formats == NULL)
        return 0;
    result = xrEnumerateSwapchainFormats(gOpenXR.session, format_count, &format_count, formats);
    if (!OpenXRResultOK(result, "xrEnumerateSwapchainFormats(HUD)")) {
        free(formats);
        return 0;
    }
    for (i = 0; i < format_count; i++) {
        if (formats[i] == GL_SRGB8_ALPHA8) {
            chosen_format = formats[i];
            break;
        }
    }
    if (chosen_format == 0 && format_count > 0)
        chosen_format = formats[0];

    memset(&create_info, 0, sizeof(create_info));
    create_info.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
    create_info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
        XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    create_info.sampleCount = 1;
    create_info.width = gOpenXR.config_views[0].recommendedImageRectWidth;
    create_info.height = gOpenXR.config_views[0].recommendedImageRectHeight;
    create_info.faceCount = 1;
    create_info.arraySize = 1;
    create_info.mipCount = 1;
    for (i = 0; i < format_count; i++) {
        uint32_t index = (chosen_format == GL_SRGB8_ALPHA8) ? 0 : i;
        if (chosen_format == GL_SRGB8_ALPHA8) {
            for (index = 0; index < format_count && formats[index] != GL_SRGB8_ALPHA8; index++) {}
            if (index >= format_count)
                continue;
        }
        create_info.format = formats[index];
        result = xrCreateSwapchain(gOpenXR.session, &create_info, &gOpenXR.hud_swapchain);
        if (XR_SUCCEEDED(result)) {
            swapchain_result = result;
            break;
        }
        initprintf("OpenXR: HUD xrCreateSwapchain format %lld failed (%d)",
            (long long)create_info.format, (int)result);
        if (chosen_format == GL_SRGB8_ALPHA8)
            chosen_format = 0;
    }
    free(formats);
    if (!OpenXRResultOK(swapchain_result, "xrCreateSwapchain(HUD)"))
        return 0;

    result = xrEnumerateSwapchainImages(gOpenXR.hud_swapchain, 0, &image_count, NULL);
    if (!OpenXRResultOK(result, "xrEnumerateSwapchainImages(HUD count)"))
        return 0;
    if (image_count > DUKEVR_OPENXR_MAX_IMAGES)
        image_count = DUKEVR_OPENXR_MAX_IMAGES;
    gOpenXR.hud_image_count = image_count;
    for (i = 0; i < image_count; i++)
        gOpenXR.hud_images[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
    result = xrEnumerateSwapchainImages(gOpenXR.hud_swapchain, image_count, &image_count,
        (XrSwapchainImageBaseHeader*)gOpenXR.hud_images);
    return OpenXRResultOK(result, "xrEnumerateSwapchainImages(HUD)");
}

static int OpenXRCreateMovableSwapchain(int layer) {
    XrSwapchainCreateInfo create_info;
    uint32_t format_count = 0;
    int64_t* formats;
    uint32_t i;
    int64_t chosen_format = 0;
    XrResult result;
    XrResult swapchain_result = XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED;
    uint32_t image_count = 0;

    if (layer < 0 || layer >= DUKEVR_OPENXR_MOVABLE_LAYERS)
        return 0;
    result = xrEnumerateSwapchainFormats(gOpenXR.session, 0, &format_count, NULL);
    if (!OpenXRResultOK(result, "xrEnumerateSwapchainFormats(movable count)"))
        return 0;
    formats = (int64_t*)calloc(format_count, sizeof(*formats));
    if (formats == NULL)
        return 0;
    result = xrEnumerateSwapchainFormats(gOpenXR.session, format_count, &format_count, formats);
    if (!OpenXRResultOK(result, "xrEnumerateSwapchainFormats(movable)")) {
        free(formats);
        return 0;
    }
    for (i = 0; i < format_count; ++i) {
        if (formats[i] == GL_SRGB8_ALPHA8) {
            chosen_format = formats[i];
            break;
        }
    }
    if (chosen_format == 0 && format_count > 0)
        chosen_format = formats[0];

    memset(&create_info, 0, sizeof(create_info));
    create_info.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
    create_info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
        XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    create_info.sampleCount = 1;
    create_info.width = gOpenXR.config_views[0].recommendedImageRectWidth;
    create_info.height = gOpenXR.config_views[0].recommendedImageRectHeight;
    create_info.faceCount = 1;
    create_info.arraySize = 1;
    create_info.mipCount = 1;
    for (i = 0; i < format_count; ++i) {
        uint32_t index = (chosen_format == GL_SRGB8_ALPHA8) ? 0 : i;
        if (chosen_format == GL_SRGB8_ALPHA8) {
            for (index = 0; index < format_count && formats[index] != GL_SRGB8_ALPHA8; ++index) {}
            if (index >= format_count)
                continue;
        }
        create_info.format = formats[index];
        result = xrCreateSwapchain(gOpenXR.session, &create_info,
            &gOpenXR.movable_swapchains[layer]);
        if (XR_SUCCEEDED(result)) {
            swapchain_result = result;
            break;
        }
        if (chosen_format == GL_SRGB8_ALPHA8)
            chosen_format = 0;
    }
    free(formats);
    if (!OpenXRResultOK(swapchain_result, "xrCreateSwapchain(movable)"))
        return 0;

    result = xrEnumerateSwapchainImages(gOpenXR.movable_swapchains[layer], 0,
        &image_count, NULL);
    if (!OpenXRResultOK(result, "xrEnumerateSwapchainImages(movable count)"))
        return 0;
    if (image_count > DUKEVR_OPENXR_MAX_IMAGES)
        image_count = DUKEVR_OPENXR_MAX_IMAGES;
    gOpenXR.movable_image_counts[layer] = image_count;
    for (i = 0; i < image_count; ++i)
        gOpenXR.movable_images[layer][i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
    result = xrEnumerateSwapchainImages(gOpenXR.movable_swapchains[layer], image_count,
        &image_count, (XrSwapchainImageBaseHeader*)gOpenXR.movable_images[layer]);
    return OpenXRResultOK(result, "xrEnumerateSwapchainImages(movable)");
}

static int DukeVROpenXR_InitializeGraphics(void) {
    XrGraphicsBindingOpenGLWin32KHR binding;
    XrSessionCreateInfo session_info;
    XrReferenceSpaceCreateInfo space_info;

    if (!gOpenXR.initialized)
        return 0;
    if (gOpenXR.graphics_initialized)
        return 1;

    memset(&binding, 0, sizeof(binding));
    binding.type = XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR;
    binding.hDC = wglGetCurrentDC();
    binding.hGLRC = wglGetCurrentContext();
    initprintf("OpenXR: GL binding hDC=%p hGLRC=%p", binding.hDC, binding.hGLRC);
    if (binding.hDC == NULL || binding.hGLRC == NULL) {
        initprintf("OpenXR: deferring graphics session until the OpenGL context exists");
        return 0;
    }

    memset(&session_info, 0, sizeof(session_info));
    session_info.type = XR_TYPE_SESSION_CREATE_INFO;
    session_info.next = &binding;
    session_info.systemId = gOpenXR.system_id;
    if (!OpenXRResultOK(xrCreateSession(gOpenXR.instance, &session_info, &gOpenXR.session), "xrCreateSession"))
        return 0;
    initprintf("OpenXR: xrCreateSession succeeded");

    if (gOpenXR.actions_created) {
        XrSessionActionSetsAttachInfo attach_info;
        XrActionSpaceCreateInfo action_space_info;
        uint32_t hand;

        memset(&attach_info, 0, sizeof(attach_info));
        attach_info.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
        attach_info.countActionSets = 1;
        attach_info.actionSets = &gOpenXR.action_set;
        if (OpenXRResultOK(xrAttachSessionActionSets(gOpenXR.session, &attach_info),
                "xrAttachSessionActionSets")) {
            gOpenXR.actions_attached = 1;
            for (hand = 0; hand < 2; hand++) {
                memset(&action_space_info, 0, sizeof(action_space_info));
                action_space_info.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
                action_space_info.action = gOpenXR.pose_action;
                action_space_info.subactionPath = gOpenXR.hand_paths[hand];
                action_space_info.poseInActionSpace.orientation.w = 1.0f;
                if (!OpenXRResultOK(xrCreateActionSpace(gOpenXR.session, &action_space_info,
                        &gOpenXR.hand_spaces[hand]), "xrCreateActionSpace(controller)")) {
                    gOpenXR.actions_attached = 0;
                    break;
                }
            }
        }
    }

    memset(&space_info, 0, sizeof(space_info));
    space_info.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    space_info.poseInReferenceSpace.orientation.w = 1.0f;
    if (!OpenXRResultOK(xrCreateReferenceSpace(gOpenXR.session, &space_info, &gOpenXR.space),
            "xrCreateReferenceSpace")) {
        OpenXRReleaseGraphics();
        return 0;
    }
    space_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    if (!OpenXRResultOK(xrCreateReferenceSpace(gOpenXR.session, &space_info, &gOpenXR.view_space),
            "xrCreateReferenceSpace(view)")) {
        OpenXRReleaseGraphics();
        return 0;
    }
    if (!OpenXRCreateSwapchain(0) || !OpenXRCreateSwapchain(1) ||
        !OpenXRCreateHudSwapchain() || !OpenXRCreateMovableSwapchain(0) ||
        !OpenXRCreateMovableSwapchain(1)) {
        OpenXRReleaseGraphics();
        return 0;
    }

    gOpenXR.graphics_initialized = 1;
    initprintf("OpenXR: graphics initialized (%ux%u per eye)",
        gOpenXR.config_views[0].recommendedImageRectWidth,
        gOpenXR.config_views[0].recommendedImageRectHeight);
    LOG_F(INFO, "OpenXR graphics initialized: %ux%u per eye",
        gOpenXR.config_views[0].recommendedImageRectWidth,
        gOpenXR.config_views[0].recommendedImageRectHeight);

    /* The XR runtime is now the frame pacer.  The mirror window uses a
     * custom size and therefore has no meaningful desktop refresh rate;
     * leaving the normal r_maxfps=-1 path active would make engineFPSLimit()
     * wait forever before the first menu frame. */
    r_maxfps = -2;
    g_frameDelay = 0;
    return 1;
}

/* Recover from a partially-started frame.  The legacy renderer can call its
 * begin/submit shims more than once while showing startup screens; every
 * successful acquire must still be released before the next OpenXR frame. */
static void OpenXRAbortFrame(void) {
    XrSwapchainImageReleaseInfo release_info;
    XrFrameEndInfo end_info;
    uint32_t i;

    if (gOpenXR.eye_render_active)
        DukeVROpenXR_EndEyeRender();

    memset(&release_info, 0, sizeof(release_info));
    release_info.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
    for (i = 0; i < 2; i++) {
        if (gOpenXR.swapchain_acquired[i] && gOpenXR.swapchains[i] != XR_NULL_HANDLE)
            xrReleaseSwapchainImage(gOpenXR.swapchains[i], &release_info);
        gOpenXR.swapchain_acquired[i] = 0;
        gOpenXR.images_acquired[i] = 0;
    }
    if (gOpenXR.hud_swapchain_acquired && gOpenXR.hud_swapchain != XR_NULL_HANDLE)
        xrReleaseSwapchainImage(gOpenXR.hud_swapchain, &release_info);
    gOpenXR.hud_swapchain_acquired = 0;
    gOpenXR.hud_image_acquired = 0;
    gOpenXR.hud_content_submitted = 0;
    for (i = 0; i < DUKEVR_OPENXR_MOVABLE_LAYERS; ++i) {
        if (gOpenXR.movable_swapchains_acquired[i] &&
            gOpenXR.movable_swapchains[i] != XR_NULL_HANDLE)
            xrReleaseSwapchainImage(gOpenXR.movable_swapchains[i], &release_info);
        gOpenXR.movable_swapchains_acquired[i] = 0;
        gOpenXR.movable_images_acquired[i] = 0;
        gOpenXR.movable_content_submitted[i] = 0;
    }
    if (gOpenXR.frame_begun && gOpenXR.session != XR_NULL_HANDLE) {
        memset(&end_info, 0, sizeof(end_info));
        end_info.type = XR_TYPE_FRAME_END_INFO;
        end_info.displayTime = gOpenXR.predicted_display_time;
        end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        OpenXRResultOK(xrEndFrame(gOpenXR.session, &end_info), "xrEndFrame(abort)");
    }
    gOpenXR.frame_begun = 0;
    gOpenXR.frame_active = 0;
    gOpenXR.scene_frame_submitted = 0;
    gOpenXR.mono_frame_submitted = 0;
}

int DukeVROpenXR_Initialize(void) {
    XrInstanceCreateInfo instance_info;
    XrSystemGetInfo system_info;
    PFN_xrGetOpenGLGraphicsRequirementsKHR get_requirements = NULL;
    XrGraphicsRequirementsOpenGLKHR requirements;
    uint32_t view_count = 0;
    uint32_t i;
    XrResult result;

    if (!DukeVROpenXR_Enabled())
        return 0;
    if (gOpenXR.initialized)
        return 1;
    if (gOpenXR.attempted)
        return 0;
    if (!OpenXRRuntimeReady())
        return 0;
    OpenXRReset();
    gOpenXR.attempted = 1;
    initprintf("OpenXR: game initialization begin");

    if (!OpenXRExtensionAvailable(XR_KHR_OPENGL_ENABLE_EXTENSION_NAME)) {
        initprintf("OpenXR: runtime does not expose XR_KHR_opengl_enable");
        return OpenXRInitializationFailed();
    }

    memset(&instance_info, 0, sizeof(instance_info));
    instance_info.type = XR_TYPE_INSTANCE_CREATE_INFO;
    strcpy(instance_info.applicationInfo.applicationName, "DukeVR OpenXR");
    strcpy(instance_info.applicationInfo.engineName, "EDuke32");
    instance_info.applicationInfo.applicationVersion = 1;
    instance_info.applicationInfo.engineVersion = 1;
    instance_info.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    instance_info.enabledExtensionCount = 1;
    {
        const char* extensions[] = { XR_KHR_OPENGL_ENABLE_EXTENSION_NAME };
        instance_info.enabledExtensionNames = extensions;
        result = xrCreateInstance(&instance_info, &gOpenXR.instance);
    }
    initprintf("OpenXR: xrCreateInstance returned %d", (int)result);
    if (!OpenXRResultOK(result, "xrCreateInstance"))
        return OpenXRInitializationFailed();

    {
        XrInstanceProperties instance_properties;
        memset(&instance_properties, 0, sizeof(instance_properties));
        instance_properties.type = XR_TYPE_INSTANCE_PROPERTIES;
    if (XR_SUCCEEDED(xrGetInstanceProperties(gOpenXR.instance, &instance_properties))) {
            gOpenXR.vdxr_runtime = strstr(instance_properties.runtimeName, "VirtualDesktopXR") != NULL;
            initprintf("OpenXR runtime: %s (version %u.%u.%u)",
                instance_properties.runtimeName,
                XR_VERSION_MAJOR(instance_properties.runtimeVersion),
                XR_VERSION_MINOR(instance_properties.runtimeVersion),
                XR_VERSION_PATCH(instance_properties.runtimeVersion));
        }
    }

    /* Controller actions are optional. A runtime that exposes the graphics
     * path but has no motion-controller profile must still run normally. */
    if (!OpenXRCreateActions())
        initprintf("OpenXR: HUD controller actions unavailable; continuing without drag input");

    memset(&system_info, 0, sizeof(system_info));
    system_info.type = XR_TYPE_SYSTEM_GET_INFO;
    system_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    initprintf("OpenXR: querying HMD system");
    if (!OpenXRResultOK(xrGetSystem(gOpenXR.instance, &system_info, &gOpenXR.system_id), "xrGetSystem"))
        return OpenXRInitializationFailed();

    if (!OpenXRResultOK(xrGetInstanceProcAddr(gOpenXR.instance, "xrGetOpenGLGraphicsRequirementsKHR",
            (PFN_xrVoidFunction*)&get_requirements), "xrGetInstanceProcAddr(xrGetOpenGLGraphicsRequirementsKHR)"))
        return OpenXRInitializationFailed();
    memset(&requirements, 0, sizeof(requirements));
    requirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR;
    if (!OpenXRResultOK(get_requirements(gOpenXR.instance, gOpenXR.system_id, &requirements),
            "xrGetOpenGLGraphicsRequirementsKHR"))
        return OpenXRInitializationFailed();

    if (!OpenXRResultOK(xrEnumerateViewConfigurationViews(gOpenXR.instance, gOpenXR.system_id,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &view_count, NULL),
            "xrEnumerateViewConfigurationViews(count)"))
        return OpenXRInitializationFailed();
    if (view_count != 2)
        return OpenXRInitializationFailed();
    for (i = 0; i < 2; i++) {
        memset(&gOpenXR.config_views[i], 0, sizeof(gOpenXR.config_views[i]));
        gOpenXR.config_views[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
    }
    if (!OpenXRResultOK(xrEnumerateViewConfigurationViews(gOpenXR.instance, gOpenXR.system_id,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &view_count, gOpenXR.config_views),
            "xrEnumerateViewConfigurationViews"))
        return OpenXRInitializationFailed();

    gOpenXR.initialized = 1;
    initprintf("OpenXR: runtime initialized (%ux%u per eye); graphics session deferred until GL setup",
        gOpenXR.config_views[0].recommendedImageRectWidth,
        gOpenXR.config_views[0].recommendedImageRectHeight);
    LOG_F(INFO, "OpenXR runtime initialized: %ux%u per eye",
        gOpenXR.config_views[0].recommendedImageRectWidth,
        gOpenXR.config_views[0].recommendedImageRectHeight);
    return 1;
}

int DukeVROpenXR_BeginFrame(void) {
    XrFrameWaitInfo wait_info;
    XrFrameState frame_state;
    XrViewLocateInfo locate_info;
    XrViewState view_state;
    XrSwapchainImageAcquireInfo acquire_info;
    XrSwapchainImageWaitInfo wait_image_info;
    uint32_t view_count = 0;
    uint32_t i;
    XrResult result;
    static int traced;
    static int logTrace;

    if (logTrace < 24)
    {
        LOG_F(INFO, "OpenXR BeginFrame %d: initialized=%d graphics=%d session=%p running=%d active=%d",
            logTrace, gOpenXR.initialized, gOpenXR.graphics_initialized,
            (void *)gOpenXR.session, gOpenXR.session_running, gOpenXR.frame_active);
        logTrace++;
    }

    if (!DukeVROpenXR_Initialize())
        return 0;
    if (!DukeVROpenXR_InitializeGraphics())
        return 0;
    if (gOpenXR.frame_active)
        return 1;
    if (!traced) {
        initprintf("OpenXR: BeginFrame initialization complete");
        traced = 1;
    }
    OpenXRPollEvents();
    if (!gOpenXR.session_running)
        return 0;

    memset(&wait_info, 0, sizeof(wait_info));
    wait_info.type = XR_TYPE_FRAME_WAIT_INFO;
    memset(&frame_state, 0, sizeof(frame_state));
    frame_state.type = XR_TYPE_FRAME_STATE;
    result = xrWaitFrame(gOpenXR.session, &wait_info, &frame_state);
    if (!OpenXRResultOK(result, "xrWaitFrame"))
        return 0;
    if (logTrace < 24)
    {
        LOG_F(INFO, "OpenXR BeginFrame: xrWaitFrame succeeded");
        logTrace++;
    }
    gOpenXR.predicted_display_time = frame_state.predictedDisplayTime;
    if (traced == 1) {
        initprintf("OpenXR: xrWaitFrame returned");
        traced = 2;
    }
    {
        XrFrameBeginInfo begin_info;
        memset(&begin_info, 0, sizeof(begin_info));
        begin_info.type = XR_TYPE_FRAME_BEGIN_INFO;
        if (!OpenXRResultOK(xrBeginFrame(gOpenXR.session, &begin_info), "xrBeginFrame"))
            return 0;
        gOpenXR.frame_begun = 1;
    }
    if (logTrace < 24)
    {
        LOG_F(INFO, "OpenXR BeginFrame: xrBeginFrame succeeded");
        logTrace++;
    }

    memset(&locate_info, 0, sizeof(locate_info));
    locate_info.type = XR_TYPE_VIEW_LOCATE_INFO;
    locate_info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate_info.displayTime = frame_state.predictedDisplayTime;
    locate_info.space = gOpenXR.space;
    memset(&view_state, 0, sizeof(view_state));
    view_state.type = XR_TYPE_VIEW_STATE;
    for (i = 0; i < 2; i++) {
        memset(&gOpenXR.views[i], 0, sizeof(gOpenXR.views[i]));
        gOpenXR.views[i].type = XR_TYPE_VIEW;
    }
    result = xrLocateViews(gOpenXR.session, &locate_info, &view_state, 2, &view_count, gOpenXR.views);
    if (!OpenXRResultOK(result, "xrLocateViews") || view_count != 2) {
        OpenXRAbortFrame();
        return 0;
    }
    if (logTrace < 24)
    {
        LOG_F(INFO, "OpenXR BeginFrame: xrLocateViews succeeded count=%u", view_count);
        logTrace++;
    }
    {
        float center_x = (gOpenXR.views[0].pose.position.x + gOpenXR.views[1].pose.position.x) * 0.5f;
        gOpenXR.eye_offset_x[0] = gOpenXR.views[0].pose.position.x - center_x;
        gOpenXR.eye_offset_x[1] = gOpenXR.views[1].pose.position.x - center_x;
        if (!gOpenXR.have_eye_geometry) {
            initprintf("OpenXR: centered eye offsets L/R=%0.4fm/%0.4fm",
                gOpenXR.eye_offset_x[0], gOpenXR.eye_offset_x[1]);
            initprintf("OpenXR: eye FOV L=%0.3f/%0.3f R=%0.3f/%0.3f U=%0.3f/%0.3f D=%0.3f/%0.3f",
                gOpenXR.views[0].fov.angleLeft, gOpenXR.views[1].fov.angleLeft,
                gOpenXR.views[0].fov.angleRight, gOpenXR.views[1].fov.angleRight,
                gOpenXR.views[0].fov.angleUp, gOpenXR.views[1].fov.angleUp,
                gOpenXR.views[0].fov.angleDown, gOpenXR.views[1].fov.angleDown);
            gOpenXR.have_eye_geometry = 1;
        }
    }
    OpenXRSyncControllerActions();
    if (getenv("OPENXR_TRACE") != NULL && traced == 2) {
        for (i = 0; i < 2; i++) {
            initprintf("[XRTRACE] eye %u: fov L/R/U/D=%0.6f %0.6f %0.6f %0.6f pose p=%0.6f %0.6f %0.6f q=%0.6f %0.6f %0.6f %0.6f\n",
                i,
                gOpenXR.views[i].fov.angleLeft, gOpenXR.views[i].fov.angleRight,
                gOpenXR.views[i].fov.angleUp, gOpenXR.views[i].fov.angleDown,
                gOpenXR.views[i].pose.position.x, gOpenXR.views[i].pose.position.y,
                gOpenXR.views[i].pose.position.z,
                gOpenXR.views[i].pose.orientation.x, gOpenXR.views[i].pose.orientation.y,
                gOpenXR.views[i].pose.orientation.z, gOpenXR.views[i].pose.orientation.w);
        }
        traced = 3;
    }

    memset(&acquire_info, 0, sizeof(acquire_info));
    acquire_info.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
    memset(&wait_image_info, 0, sizeof(wait_image_info));
    wait_image_info.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
    wait_image_info.timeout = XR_INFINITE_DURATION;
    for (i = 0; i < 2; i++) {
        result = xrAcquireSwapchainImage(gOpenXR.swapchains[i], &acquire_info, &gOpenXR.images_acquired[i]);
        if (!OpenXRResultOK(result, "xrAcquireSwapchainImage")) {
            initprintf("OpenXR: acquire failed for eye %u", i);
            OpenXRAbortFrame();
            return 0;
        }
        gOpenXR.swapchain_acquired[i] = 1;
        if (!gOpenXR.vdxr_skip_wait) {
            result = xrWaitSwapchainImage(gOpenXR.swapchains[i], &wait_image_info);
            if (!OpenXRResultOK(result, "xrWaitSwapchainImage")) {
                if (gOpenXR.vdxr_runtime && result == XR_ERROR_CALL_ORDER_INVALID) {
                    gOpenXR.vdxr_skip_wait = 1;
                    initprintf("OpenXR: VDXR rejected swapchain wait; continuing with acquired images");
                } else {
                    initprintf("OpenXR: wait failed for eye %u after acquiring image %u", i, gOpenXR.images_acquired[i]);
                    OpenXRAbortFrame();
                    return 0;
                }
            }
        }
    }
    result = xrAcquireSwapchainImage(gOpenXR.hud_swapchain, &acquire_info,
        &gOpenXR.hud_image_acquired);
    if (!OpenXRResultOK(result, "xrAcquireSwapchainImage(HUD)")) {
        OpenXRAbortFrame();
        return 0;
    }
    gOpenXR.hud_swapchain_acquired = 1;
    if (!gOpenXR.vdxr_skip_wait) {
        result = xrWaitSwapchainImage(gOpenXR.hud_swapchain, &wait_image_info);
        if (!OpenXRResultOK(result, "xrWaitSwapchainImage(HUD)")) {
            if (gOpenXR.vdxr_runtime && result == XR_ERROR_CALL_ORDER_INVALID) {
                gOpenXR.vdxr_skip_wait = 1;
                initprintf("OpenXR: VDXR rejected HUD swapchain wait; continuing with acquired image");
            } else {
                OpenXRAbortFrame();
                return 0;
            }
        }
    }
    for (i = 0; i < DUKEVR_OPENXR_MOVABLE_LAYERS; ++i) {
        result = xrAcquireSwapchainImage(gOpenXR.movable_swapchains[i], &acquire_info,
            &gOpenXR.movable_images_acquired[i]);
        if (!OpenXRResultOK(result, "xrAcquireSwapchainImage(movable)")) {
            OpenXRAbortFrame();
            return 0;
        }
        gOpenXR.movable_swapchains_acquired[i] = 1;
        if (!gOpenXR.vdxr_skip_wait) {
            result = xrWaitSwapchainImage(gOpenXR.movable_swapchains[i], &wait_image_info);
            if (!OpenXRResultOK(result, "xrWaitSwapchainImage(movable)")) {
                if (gOpenXR.vdxr_runtime && result == XR_ERROR_CALL_ORDER_INVALID)
                    gOpenXR.vdxr_skip_wait = 1;
                else {
                    OpenXRAbortFrame();
                    return 0;
                }
            }
        }
    }
    gOpenXR.frame_active = 1;
#ifdef POLYMER
    OpenXRApplyRendererFov();
#endif
    if (logTrace < 24)
    {
        LOG_F(INFO, "OpenXR BeginFrame: all swapchain images acquired");
        logTrace++;
    }
    return 1;
}

int DukeVROpenXR_PrepareInput(void) {
    return DukeVROpenXR_BeginFrame();
}

int DukeVROpenXR_FrameActive(void) {
    return gOpenXR.frame_active;
}

void DukeVROpenXR_MarkSceneFrame(void) {
    gOpenXR.scene_frame_submitted = 1;
}

void DukeVROpenXR_MarkMonoFrame(void) {
    gOpenXR.mono_frame_submitted = 1;
}

int DukeVROpenXR_ConsumeSceneFrame(void) {
    int submitted = gOpenXR.scene_frame_submitted;
    gOpenXR.scene_frame_submitted = 0;
    return submitted;
}

int DukeVROpenXR_GetEyePose(int eye, float orientation_xyzw[4], float position_xyz[3]) {
    if (!gOpenXR.frame_active || eye < 0 || eye > 1)
        return 0;
    orientation_xyzw[0] = gOpenXR.views[eye].pose.orientation.x;
    orientation_xyzw[1] = gOpenXR.views[eye].pose.orientation.y;
    orientation_xyzw[2] = gOpenXR.views[eye].pose.orientation.z;
    orientation_xyzw[3] = gOpenXR.views[eye].pose.orientation.w;
    position_xyz[0] = gOpenXR.views[eye].pose.position.x;
    position_xyz[1] = gOpenXR.views[eye].pose.position.y;
    position_xyz[2] = gOpenXR.views[eye].pose.position.z;
    return 1;
}

int DukeVROpenXR_GetControllerPose(int hand, float position_xyz[3],
    float orientation_xyzw[4], int* grip_pressed) {
    if (!gOpenXR.frame_active || hand < 0 || hand > 1 || !gOpenXR.controller_valid[hand])
        return 0;
    if (position_xyz != NULL) {
        position_xyz[0] = gOpenXR.controller_position[hand][0];
        position_xyz[1] = gOpenXR.controller_position[hand][1];
        position_xyz[2] = gOpenXR.controller_position[hand][2];
    }
    if (orientation_xyzw != NULL) {
        orientation_xyzw[0] = gOpenXR.controller_orientation[hand][0];
        orientation_xyzw[1] = gOpenXR.controller_orientation[hand][1];
        orientation_xyzw[2] = gOpenXR.controller_orientation[hand][2];
        orientation_xyzw[3] = gOpenXR.controller_orientation[hand][3];
    }
    if (grip_pressed != NULL)
        *grip_pressed = gOpenXR.controller_grip[hand];
    return 1;
}

int DukeVROpenXR_GetEyeFov(int eye, float* angle_left, float* angle_right,
    float* angle_up, float* angle_down) {
    if (!gOpenXR.frame_active || eye < 0 || eye > 1 ||
        angle_left == NULL || angle_right == NULL || angle_up == NULL || angle_down == NULL)
        return 0;
    *angle_left = gOpenXR.views[eye].fov.angleLeft;
    *angle_right = gOpenXR.views[eye].fov.angleRight;
    *angle_up = gOpenXR.views[eye].fov.angleUp;
    *angle_down = gOpenXR.views[eye].fov.angleDown;
    return 1;
}

void DukeVROpenXR_SetRenderOrientationResidual(float yaw_radians,
    float pitch_radians, float roll_radians) {
    gOpenXR.render_yaw_residual = yaw_radians;
    gOpenXR.render_pitch_residual = pitch_radians;
    gOpenXR.render_roll_residual = roll_radians;
}

void DukeVROpenXR_GetRenderOrientationResidual(float* yaw_radians,
    float* pitch_radians, float* roll_radians) {
    if (yaw_radians != NULL)
        *yaw_radians = gOpenXR.render_yaw_residual;
    if (pitch_radians != NULL)
        *pitch_radians = gOpenXR.render_pitch_residual;
    if (roll_radians != NULL)
        *roll_radians = gOpenXR.render_roll_residual;
}

void DukeVROpenXR_SetRenderPositionResidual(float x_build, float y_build,
    float z_build) {
    gOpenXR.render_pos_residual[0] = x_build;
    gOpenXR.render_pos_residual[1] = y_build;
    gOpenXR.render_pos_residual[2] = z_build;
}

void DukeVROpenXR_GetRenderPositionResidual(float* x_build, float* y_build,
    float* z_build) {
    if (x_build != NULL)
        *x_build = gOpenXR.render_pos_residual[0];
    if (y_build != NULL)
        *y_build = gOpenXR.render_pos_residual[1];
    if (z_build != NULL)
        *z_build = gOpenXR.render_pos_residual[2];
}

int DukeVROpenXR_GetEyeDimensions(int eye, int* width, int* height) {
    if (!gOpenXR.initialized || eye < 0 || eye > 1 || width == NULL || height == NULL)
        return 0;
    *width = (int)gOpenXR.config_views[eye].recommendedImageRectWidth;
    *height = (int)gOpenXR.config_views[eye].recommendedImageRectHeight;
    return *width > 0 && *height > 0;
}

int DukeVROpenXR_GetSceneDimensions(int* width, int* height) {
    if (width == NULL || height == NULL || gOpenXR.scene_target_width <= 0 ||
        gOpenXR.scene_target_height <= 0)
        return 0;
    *width = gOpenXR.scene_target_width;
    *height = gOpenXR.scene_target_height;
    return 1;
}

uint32_t DukeVROpenXR_GetEyeTexture(int eye) {
    uint32_t image_index;
    if (!gOpenXR.graphics_initialized || eye < 0 || eye > 1)
        return 0;
    image_index = gOpenXR.frame_active ? gOpenXR.images_acquired[eye] : 0;
    if (image_index >= gOpenXR.image_counts[eye])
        return 0;
    return (uint32_t)gOpenXR.images[eye][image_index].image;
}

static int OpenXREnsureSceneTarget(int eye, int width, int height) {
    GLuint framebuffer = 0;
    GLuint texture = 0;
    GLuint depth = 0;

    if (eye < 0 || eye > 1 || width <= 0 || height <= 0)
        return 0;
    if (gOpenXR.scene_target_width != 0 &&
        (gOpenXR.scene_target_width != width || gOpenXR.scene_target_height != height))
        OpenXRReleaseSceneTargets();
    if (gOpenXR.scene_fbos[eye] != 0 && gOpenXR.scene_textures[eye] != 0 &&
        gOpenXR.scene_depth[eye] != 0)
        return 1;

    glGenFramebuffers(1, &framebuffer);
    glGenTextures(1, &texture);
    glGenRenderbuffers(1, &depth);
    if (framebuffer == 0 || texture == 0 || depth == 0) {
        if (framebuffer != 0) glDeleteFramebuffers(1, &framebuffer);
        if (texture != 0) glDeleteTextures(1, &texture);
        if (depth != 0) glDeleteRenderbuffers(1, &depth);
        return 0;
    }

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &framebuffer);
        glDeleteTextures(1, &texture);
        glDeleteRenderbuffers(1, &depth);
        return 0;
    }

    gOpenXR.scene_fbos[eye] = framebuffer;
    gOpenXR.scene_textures[eye] = texture;
    gOpenXR.scene_depth[eye] = depth;
    gOpenXR.scene_target_width = width;
    gOpenXR.scene_target_height = height;
    return 1;
}

static int OpenXREnsureHudTarget(int width, int height) {
    if (width <= 0 || height <= 0)
        return 0;
    if (gOpenXR.hud_fbo != 0 &&
        (gOpenXR.hud_target_width != width || gOpenXR.hud_target_height != height))
        OpenXRReleaseHudTarget();
    if (gOpenXR.hud_fbo != 0 && gOpenXR.hud_texture != 0 &&
        gOpenXR.hud_depth != 0 && gOpenXR.hud_runtime_fbo != 0)
        return 1;

    glGenFramebuffers(1, &gOpenXR.hud_fbo);
    glGenTextures(1, &gOpenXR.hud_texture);
    glGenRenderbuffers(1, &gOpenXR.hud_depth);
    glGenFramebuffers(1, &gOpenXR.hud_runtime_fbo);
    if (gOpenXR.hud_fbo == 0 || gOpenXR.hud_texture == 0 ||
        gOpenXR.hud_depth == 0 || gOpenXR.hud_runtime_fbo == 0) {
        OpenXRReleaseHudTarget();
        return 0;
    }

    glBindTexture(GL_TEXTURE_2D, gOpenXR.hud_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindRenderbuffer(GL_RENDERBUFFER, gOpenXR.hud_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, gOpenXR.hud_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
        gOpenXR.hud_texture, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
        GL_RENDERBUFFER, gOpenXR.hud_depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        OpenXRReleaseHudTarget();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return 0;
    }
    gOpenXR.hud_target_width = width;
    gOpenXR.hud_target_height = height;
    return 1;
}

static int OpenXREnsureMovableTargets(int width, int height) {
    int layer;
    if (width <= 0 || height <= 0)
        return 0;
    if (gOpenXR.movable_fbos[0] != 0 &&
        (gOpenXR.movable_target_width != width ||
         gOpenXR.movable_target_height != height))
        OpenXRReleaseMovableTargets();
    if (gOpenXR.movable_fbos[0] != 0 && gOpenXR.movable_fbos[1] != 0)
        return 1;

    glGenFramebuffers(DUKEVR_OPENXR_MOVABLE_LAYERS, gOpenXR.movable_fbos);
    glGenTextures(DUKEVR_OPENXR_MOVABLE_LAYERS, gOpenXR.movable_textures);
    glGenRenderbuffers(DUKEVR_OPENXR_MOVABLE_LAYERS, gOpenXR.movable_depth);
    glGenFramebuffers(DUKEVR_OPENXR_MOVABLE_LAYERS, gOpenXR.movable_runtime_fbos);
    for (layer = 0; layer < DUKEVR_OPENXR_MOVABLE_LAYERS; ++layer) {
        if (gOpenXR.movable_fbos[layer] == 0 || gOpenXR.movable_textures[layer] == 0 ||
            gOpenXR.movable_depth[layer] == 0 || gOpenXR.movable_runtime_fbos[layer] == 0) {
            OpenXRReleaseMovableTargets();
            return 0;
        }
        glBindTexture(GL_TEXTURE_2D, gOpenXR.movable_textures[layer]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glBindRenderbuffer(GL_RENDERBUFFER, gOpenXR.movable_depth[layer]);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glBindFramebuffer(GL_FRAMEBUFFER, gOpenXR.movable_fbos[layer]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
            gOpenXR.movable_textures[layer], 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
            GL_RENDERBUFFER, gOpenXR.movable_depth[layer]);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            OpenXRReleaseMovableTargets();
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return 0;
        }
    }
    gOpenXR.movable_target_width = width;
    gOpenXR.movable_target_height = height;
    return 1;
}

/* Render Build into a moderately sized intermediate target with the same
 * aspect as the runtime eye, then scale it to the complete runtime eye image
 * in EndEyeRender. Build's logical buffers and the desktop mirror remain
 * independent of this target. */
int DukeVROpenXR_BeginEyeRender(int eye) {
    GLuint texture;
    int width = 0, height = 0;
    int scene_width, scene_height;

    if (!gOpenXR.frame_active || eye < 0 || eye > 1 || gOpenXR.eye_render_active)
        return 0;
    texture = DukeVROpenXR_GetEyeTexture(eye);
    if (!texture || !DukeVROpenXR_GetEyeDimensions(eye, &width, &height))
        return 0;
    scene_width = xdim;
    scene_height = (int)((float)scene_width * (float)height / (float)width + .5f);
    if (scene_height < 1)
        scene_height = 1;
    if (!OpenXREnsureSceneTarget(eye, scene_width, scene_height))
        return 0;
    if (gOpenXR.eye_fbos[eye] == 0)
        glGenFramebuffers(1, &gOpenXR.eye_fbos[eye]);
    if (gOpenXR.eye_depth[eye] == 0)
        glGenRenderbuffers(1, &gOpenXR.eye_depth[eye]);
    if (gOpenXR.eye_fbos[eye] == 0 || gOpenXR.eye_depth[eye] == 0)
        return 0;

    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &gOpenXR.saved_draw_framebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &gOpenXR.saved_read_framebuffer);
    glGetIntegerv(GL_VIEWPORT, gOpenXR.saved_viewport);
    glBindRenderbuffer(GL_RENDERBUFFER, gOpenXR.eye_depth[eye]);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glBindFramebuffer(GL_FRAMEBUFFER, gOpenXR.scene_fbos[eye]);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)gOpenXR.saved_draw_framebuffer);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)gOpenXR.saved_read_framebuffer);
        return 0;
    }
    glViewport(0, 0, scene_width, scene_height);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    gOpenXR.current_eye = eye;
    gOpenXR.eye_render_active = 1;
    return 1;
}

int DukeVROpenXR_BeginHudRender(void) {
    int width = xdim;
    int height = ydim;

    if (!gOpenXR.frame_active || !gOpenXR.eye_render_active ||
        gOpenXR.hud_render_active || !OpenXREnsureHudTarget(width, height) ||
        !OpenXREnsureMovableTargets(width, height))
        return 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &gOpenXR.saved_hud_draw_framebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &gOpenXR.saved_hud_read_framebuffer);
    glGetIntegerv(GL_VIEWPORT, gOpenXR.saved_hud_viewport);
    glBindFramebuffer(GL_FRAMEBUFFER, gOpenXR.hud_fbo);
    glViewport(0, 0, width, height);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    for (int layer = 0; layer < DUKEVR_OPENXR_MOVABLE_LAYERS; ++layer) {
        glBindFramebuffer(GL_FRAMEBUFFER, gOpenXR.movable_fbos[layer]);
        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        gOpenXR.movable_content_submitted[layer] = 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, gOpenXR.hud_fbo);
    glViewport(0, 0, width, height);
    gOpenXR.hud_render_active = 1;
    return 1;
}

int DukeVROpenXR_BeginHudLayer(int layer) {
    int index = layer - 1;
    if (!gOpenXR.hud_render_active || gOpenXR.movable_render_active ||
        index < 0 || index >= DUKEVR_OPENXR_MOVABLE_LAYERS)
        return 0;
    glBindFramebuffer(GL_FRAMEBUFFER, gOpenXR.movable_fbos[index]);
    glViewport(0, 0, gOpenXR.movable_target_width, gOpenXR.movable_target_height);
    gOpenXR.movable_render_active = layer;
    return 1;
}

void DukeVROpenXR_EndHudLayer(void) {
    int index = gOpenXR.movable_render_active - 1;
    if (index < 0 || index >= DUKEVR_OPENXR_MOVABLE_LAYERS)
        return;
    gOpenXR.movable_content_submitted[index] = 1;
    glBindFramebuffer(GL_FRAMEBUFFER, gOpenXR.hud_fbo);
    glViewport(0, 0, gOpenXR.hud_target_width, gOpenXR.hud_target_height);
    gOpenXR.movable_render_active = 0;
}

void DukeVROpenXR_SetHudLayerOffsets(int weapon_x, int weapon_y,
    int status_x, int status_y) {
    gOpenXR.movable_offsets[0][0] = weapon_x;
    gOpenXR.movable_offsets[0][1] = weapon_y;
    gOpenXR.movable_offsets[1][0] = status_x;
    gOpenXR.movable_offsets[1][1] = status_y;
}

void DukeVROpenXR_EndHudRender(void) {
    GLuint texture;
    int width = 0, height = 0;
    int copied = 0;
    static int logged;

    if (!gOpenXR.hud_render_active)
        return;
    if (gOpenXR.movable_render_active)
        DukeVROpenXR_EndHudLayer();
    texture = DukeVROpenXR_GetHudTexture();
    if (texture != 0 && DukeVROpenXR_GetEyeDimensions(0, &width, &height)) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, gOpenXR.hud_fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gOpenXR.hud_runtime_fbo);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D, texture, 0);
        if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
            glBlitFramebuffer(0, 0, gOpenXR.hud_target_width, gOpenXR.hud_target_height,
                0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
            copied = 1;
        }
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D, 0, 0);
        if (copied) {
            gOpenXR.hud_content_submitted = 1;
            if (!logged) {
                LOG_F(INFO, "OpenXR HUD texture submitted: source=%dx%d runtime=%dx%d",
                    gOpenXR.hud_target_width, gOpenXR.hud_target_height, width, height);
                logged = 1;
            }
        } else if (!logged) {
            LOG_F(ERROR, "OpenXR HUD framebuffer incomplete while copying HUD texture");
            logged = 1;
        }
    }
    for (int layer = 0; layer < DUKEVR_OPENXR_MOVABLE_LAYERS; ++layer) {
        if (width <= 0 || height <= 0 ||
            !gOpenXR.movable_content_submitted[layer] ||
            !gOpenXR.movable_swapchains_acquired[layer] ||
            gOpenXR.movable_images_acquired[layer] >= gOpenXR.movable_image_counts[layer])
            continue;
        GLuint movableTexture = gOpenXR.movable_images[layer]
            [gOpenXR.movable_images_acquired[layer]].image;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, gOpenXR.movable_fbos[layer]);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gOpenXR.movable_runtime_fbos[layer]);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D, movableTexture, 0);
        if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
            glBlitFramebuffer(0, 0, gOpenXR.movable_target_width,
                gOpenXR.movable_target_height, 0, 0, width, height,
                GL_COLOR_BUFFER_BIT, GL_LINEAR);
        } else {
            gOpenXR.movable_content_submitted[layer] = 0;
        }
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D, 0, 0);
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)gOpenXR.saved_hud_draw_framebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)gOpenXR.saved_hud_read_framebuffer);
    glViewport(gOpenXR.saved_hud_viewport[0], gOpenXR.saved_hud_viewport[1],
        gOpenXR.saved_hud_viewport[2], gOpenXR.saved_hud_viewport[3]);
    gOpenXR.hud_render_active = 0;
}

void DukeVROpenXR_SetHudMenuScale(int scaled) {
    if (gOpenXR.frame_active)
        gOpenXR.hud_menu_scaled = scaled != 0;
}

static int OpenXRBlitSceneToEyeTexture(int eye) {
    GLuint texture;
    int width, height;
    GLenum status;
    static int logged_error;

    if (eye < 0 || eye > 1 || gOpenXR.scene_fbos[eye] == 0)
        return 0;
    texture = DukeVROpenXR_GetEyeTexture(eye);
    if (texture == 0 || !DukeVROpenXR_GetEyeDimensions(eye, &width, &height))
        return 0;

    glBindFramebuffer(GL_READ_FRAMEBUFFER, gOpenXR.scene_fbos[eye]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, gOpenXR.eye_fbos[eye]);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, texture, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, gOpenXR.eye_depth[eye]);
    glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
        GL_RENDERBUFFER, gOpenXR.eye_depth[eye]);
    status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        if (!logged_error) {
            LOG_F(ERROR, "OpenXR eye %d framebuffer incomplete while copying scene: 0x%x",
                eye, (unsigned int)status);
            logged_error = 1;
        }
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D, 0, 0);
        return 0;
    }

    glBlitFramebuffer(0, 0, gOpenXR.scene_target_width, gOpenXR.scene_target_height,
        0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    if (glGetError() != GL_NO_ERROR && !logged_error) {
        LOG_F(ERROR, "OpenXR eye %d scene-to-runtime blit generated an OpenGL error", eye);
        logged_error = 1;
    }
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, 0, 0);
    return 1;
}

void DukeVROpenXR_EndEyeRender(void) {
    if (!gOpenXR.eye_render_active)
        return;
    OpenXRBlitSceneToEyeTexture(gOpenXR.current_eye);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)gOpenXR.saved_draw_framebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)gOpenXR.saved_read_framebuffer);
    glViewport(gOpenXR.saved_viewport[0], gOpenXR.saved_viewport[1],
        gOpenXR.saved_viewport[2], gOpenXR.saved_viewport[3]);
    gOpenXR.eye_render_active = 0;
    gOpenXR.current_eye = -1;
}

int DukeVROpenXR_CurrentEye(void) {
    return gOpenXR.eye_render_active ? gOpenXR.current_eye : -1;
}

int DukeVROpenXR_SceneFrameActive(void) {
    return gOpenXR.frame_active && gOpenXR.scene_frame_submitted;
}

/* Put the final eye image on the desktop window. This keeps the monitor
 * useful while the headset receives the original per-eye projection layer. */
int DukeVROpenXR_PresentMirror(int eye, int width, int height) {
    GLint old_draw, old_read;
    GLuint framebuffer = 0;
    GLuint texture;
    GLuint source_framebuffer;
    int xr_width, xr_height;

    if (!gOpenXR.frame_active || eye < 0 || eye > 1 || width <= 0 || height <= 0 ||
        !DukeVROpenXR_GetEyeDimensions(eye, &xr_width, &xr_height))
        return 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_draw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read);
    /* videoShowFrame() calls this before EndEyeRender(). Finish the same
     * scene-to-runtime copy that EndEyeRender() performs so the desktop is a
     * diagnostic mirror of the exact texture sent to OpenXR, not merely of
     * the intermediate desktop-sized scene target. */
    if (gOpenXR.eye_render_active && gOpenXR.current_eye == eye &&
        !OpenXRBlitSceneToEyeTexture(eye))
        return 0;

    texture = DukeVROpenXR_GetEyeTexture(eye);
    if (!texture)
        return 0;
    glGenFramebuffers(1, &framebuffer);
    if (!framebuffer)
        return 0;
    source_framebuffer = framebuffer;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, source_framebuffer);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, source_framebuffer);
    if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
    {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, xr_width, xr_height, 0, 0, width, height,
            GL_COLOR_BUFFER_BIT, GL_LINEAR);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)old_read);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)old_draw);
    if (framebuffer != 0)
        glDeleteFramebuffers(1, &framebuffer);
    return 1;
}

uint32_t DukeVROpenXR_GetHudTexture(void) {
    if (!gOpenXR.frame_active || !gOpenXR.hud_swapchain_acquired ||
        gOpenXR.hud_image_acquired >= gOpenXR.hud_image_count)
        return 0;
    return (uint32_t)gOpenXR.hud_images[gOpenXR.hud_image_acquired].image;
}

void DukeVROpenXR_MarkHudFrame(void) {
    if (gOpenXR.frame_active)
        gOpenXR.hud_content_submitted = 1;
}

/* Submit one already-rendered BRender image to both eyes.  This is used by
 * the front-end/menu path, which has no per-eye scene render but still needs
 * to keep the OpenXR session alive and visible in the headset. */
int DukeVROpenXR_SubmitTexture(void* source) {
    (void)source;
    return 0;
}

/* The title/logo and setup paths render one mono frame directly to the
 * desktop framebuffer instead of running the normal two-eye scene loop.
 * Put that frame in the HUD swapchain and submit it as a head-locked quad so
 * desktop aspect/FOV assumptions cannot crop the front-end screens. */
int DukeVROpenXR_SubmitDesktopFrame(int width, int height) {
    GLuint framebuffer = 0;
    GLint old_read = 0;
    GLint old_draw = 0;
    GLint old_read_buffer = GL_BACK;
    GLint old_draw_buffer = GL_BACK;
    int xr_width = 0;
    int xr_height = 0;
    int ok = 1;
    GLuint hud_texture;
    static int logged;
    static int loggedQuad;
    static int traceCount;

    if (traceCount < 24)
    {
        LOG_F(INFO, "OpenXR mono submit %d: enter desktop=%dx%d", traceCount, width, height);
        traceCount++;
    }

    if (!DukeVROpenXR_BeginFrame())
    {
        if (traceCount < 24)
        {
            LOG_F(INFO, "OpenXR mono submit: BeginFrame returned false");
            traceCount++;
        }
        return 0;
    }
    if (traceCount < 24)
    {
        LOG_F(INFO, "OpenXR mono submit: BeginFrame returned true");
        traceCount++;
    }
    DukeVROpenXR_MarkMonoFrame();
    if (!DukeVROpenXR_GetEyeDimensions(0, &xr_width, &xr_height) ||
        width <= 0 || height <= 0) {
        DukeVROpenXR_EndFrame();
        return 0;
    }
    if (!logged) {
        initprintf("OpenXR: submitting mono desktop frame (%dx%d) to both eyes (%dx%d)",
            width, height, xr_width, xr_height);
        logged = 1;
    }

    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_draw);
    /* Polymer and the OpenXR auxiliary targets use their own draw/read
     * selections. Binding framebuffer 0 does not reliably restore the
     * window back-buffer selection on all drivers, so make the source and
     * destination buffers explicit for synchronous front-end screens such
     * as the mission bonus screen. */
    glGetIntegerv(GL_READ_BUFFER, &old_read_buffer);
    glGetIntegerv(GL_DRAW_BUFFER, &old_draw_buffer);
    glGenFramebuffers(1, &framebuffer);
    if (framebuffer == 0) {
        LOG_F(ERROR, "OpenXR mono submit: glGenFramebuffers returned zero");
        DukeVROpenXR_EndFrame();
        return 0;
    }

    hud_texture = DukeVROpenXR_GetHudTexture();
    if (hud_texture == 0) {
        LOG_F(ERROR, "OpenXR mono submit: HUD swapchain image unavailable");
        ok = 0;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    if (ok) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D, hud_texture, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            LOG_F(ERROR, "OpenXR mono submit: HUD framebuffer incomplete");
            ok = 0;
        } else {
            glBlitFramebuffer(0, 0, width, height, 0, 0, xr_width, xr_height,
                GL_COLOR_BUFFER_BIT, GL_LINEAR);
        }
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D, 0, 0);
    }
    glDeleteFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)old_read);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)old_draw);
    glReadBuffer((GLenum)old_read_buffer);
    glDrawBuffer((GLenum)old_draw_buffer);

    if (ok)
        glFlush();
    if (traceCount < 24)
    {
        LOG_F(INFO, "OpenXR mono submit: desktop copy complete ok=%d", ok);
        traceCount++;
    }
    if (ok) {
        gOpenXR.mono_quad_submitted = 1;
        gOpenXR.hud_content_submitted = 1;
        gOpenXR.mono_source_width = width;
        gOpenXR.mono_source_height = height;
        if (!loggedQuad) {
            LOG_F(INFO, "OpenXR mono desktop frame submitted as head-locked quad: source=%dx%d runtime=%dx%d",
                width, height, xr_width, xr_height);
            loggedQuad = 1;
        }
    }
    DukeVROpenXR_EndFrame();
    if (traceCount < 24)
    {
        LOG_F(INFO, "OpenXR mono submit: EndFrame returned");
        traceCount++;
    }
    return ok;
}

void DukeVROpenXR_EndFrame(void) {
    XrFrameEndInfo end_info;
    XrCompositionLayerProjection projection_layer;
    XrCompositionLayerQuad hud_layer;
    XrCompositionLayerQuad movable_layers[DUKEVR_OPENXR_MOVABLE_LAYERS];
    const XrCompositionLayerBaseHeader* layers[2 + DUKEVR_OPENXR_MOVABLE_LAYERS];
    uint32_t layer_count = 0;
    uint32_t i;

    if (gOpenXR.eye_render_active)
        DukeVROpenXR_EndEyeRender();
    if (!gOpenXR.frame_active) {
        if (gOpenXR.frame_begun)
            OpenXRAbortFrame();
        return;
    }
    memset(&projection_layer, 0, sizeof(projection_layer));
    projection_layer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
    projection_layer.space = gOpenXR.space;
    projection_layer.viewCount = 2;
    projection_layer.views = gOpenXR.layer_views;

    if (gOpenXR.mono_frame_submitted)
    {
        XrPosef mono_pose = gOpenXR.views[0].pose;
        XrFovf mono_fov;
        float horizontal = 0.0f;
        float vertical = 0.0f;

        mono_pose.position.x = (gOpenXR.views[0].pose.position.x +
            gOpenXR.views[1].pose.position.x) * 0.5f;
        mono_pose.position.y = (gOpenXR.views[0].pose.position.y +
            gOpenXR.views[1].pose.position.y) * 0.5f;
        mono_pose.position.z = (gOpenXR.views[0].pose.position.z +
            gOpenXR.views[1].pose.position.z) * 0.5f;
        horizontal = ((-gOpenXR.views[0].fov.angleLeft + gOpenXR.views[0].fov.angleRight) +
            (-gOpenXR.views[1].fov.angleLeft + gOpenXR.views[1].fov.angleRight)) * 0.25f;
        vertical = ((gOpenXR.views[0].fov.angleUp - gOpenXR.views[0].fov.angleDown) +
            (gOpenXR.views[1].fov.angleUp - gOpenXR.views[1].fov.angleDown)) * 0.25f;
        memset(&mono_fov, 0, sizeof(mono_fov));
        mono_fov.angleLeft = -horizontal;
        mono_fov.angleRight = horizontal;
        mono_fov.angleUp = vertical;
        mono_fov.angleDown = -vertical;

        for (i = 0; i < 2; i++) {
            memset(&gOpenXR.layer_views[i], 0, sizeof(gOpenXR.layer_views[i]));
            gOpenXR.layer_views[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            gOpenXR.layer_views[i].pose = mono_pose;
            gOpenXR.layer_views[i].fov = mono_fov;
        }
    }
    for (i = 0; i < 2; i++) {
        if (!gOpenXR.mono_frame_submitted) {
            memset(&gOpenXR.layer_views[i], 0, sizeof(gOpenXR.layer_views[i]));
            gOpenXR.layer_views[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            gOpenXR.layer_views[i].pose = gOpenXR.views[i].pose;
            gOpenXR.layer_views[i].fov = gOpenXR.views[i].fov;
        }
        gOpenXR.layer_views[i].subImage.swapchain = gOpenXR.swapchains[i];
        gOpenXR.layer_views[i].subImage.imageRect.offset.x = 0;
        gOpenXR.layer_views[i].subImage.imageRect.offset.y = 0;
        /* Scene frames are expanded into the complete runtime eye image by
         * EndEyeRender, so every projection frame uses the full rectangle. */
        gOpenXR.layer_views[i].subImage.imageRect.extent.width = gOpenXR.config_views[i].recommendedImageRectWidth;
        gOpenXR.layer_views[i].subImage.imageRect.extent.height = gOpenXR.config_views[i].recommendedImageRectHeight;
        gOpenXR.layer_views[i].subImage.imageArrayIndex = 0;
    }
    const int hud_ready = gOpenXR.hud_content_submitted && gOpenXR.hud_swapchain_acquired;
    if (hud_ready)
    {
        float horizontal = ((-gOpenXR.views[0].fov.angleLeft + gOpenXR.views[0].fov.angleRight) +
            (-gOpenXR.views[1].fov.angleLeft + gOpenXR.views[1].fov.angleRight)) * 0.25f;
        float aspect = (float)gOpenXR.hud_target_width / (float)gOpenXR.hud_target_height;
        const float distance = 3.0f;

        if (gOpenXR.mono_quad_submitted && gOpenXR.mono_source_width > 0 &&
            gOpenXR.mono_source_height > 0)
            aspect = (float)gOpenXR.mono_source_width / (float)gOpenXR.mono_source_height;
        if (aspect <= 0.0f)
            aspect = 1.0f;

        memset(&hud_layer, 0, sizeof(hud_layer));
        hud_layer.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
        /* Mono front-end pages (title, menus, and the mission bonus screen)
         * are copied from the desktop framebuffer. Build clears that buffer
         * with alpha 0, so blending the copied page can leave its background
         * transparent after returning from a stereo gameplay frame. Submit
         * these pages as opaque quads; the live gameplay HUD still uses the
         * original alpha-blended composition path below. */
        hud_layer.layerFlags = gOpenXR.mono_quad_submitted
            ? 0 : XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        hud_layer.space = gOpenXR.view_space;
        hud_layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        hud_layer.subImage.swapchain = gOpenXR.hud_swapchain;
        hud_layer.subImage.imageRect.offset.x = 0;
        hud_layer.subImage.imageRect.offset.y = 0;
        hud_layer.subImage.imageRect.extent.width = gOpenXR.config_views[0].recommendedImageRectWidth;
        hud_layer.subImage.imageRect.extent.height = gOpenXR.config_views[0].recommendedImageRectHeight;
        hud_layer.subImage.imageArrayIndex = 0;
        hud_layer.pose.orientation.w = 1.0f;
        hud_layer.pose.position.z = -distance;
        hud_layer.size.width = 2.0f * tanf(horizontal) * distance;
        /* The desktop source is copied into the square-ish runtime HUD
         * texture. Use the source aspect for the physical quad so that copy
         * does not make the menus/HUD appear optically zoomed or distorted. */
        hud_layer.size.height = hud_layer.size.width / aspect;
        /* Front-end title/setup screens are mono frames submitted through
         * this same head-locked quad and need the smaller legacy presentation
         * size. In-game mission menus already set hud_menu_scaled and retain
         * their existing size. */
        if (gOpenXR.mono_quad_submitted || gOpenXR.hud_menu_scaled)
        {
            hud_layer.size.width *= 0.5f;
            hud_layer.size.height *= 0.5f;
        }
#ifdef OCULUS
        if (oculusUseMenuScaleForHUD && !gOpenXR.hud_menu_scaled)
        {
            hud_layer.size.width *= 0.5f;
            hud_layer.size.height *= 0.5f;
        }
#endif
        if (gOpenXR.mono_quad_submitted) {
            layers[layer_count++] = (const XrCompositionLayerBaseHeader*)&hud_layer;
        } else {
            layers[layer_count++] = (const XrCompositionLayerBaseHeader*)&projection_layer;
            /* Match the original draw order: weapon, health/ammo HUD, then
             * the general HUD (crosshair, quotes and menus) on top. */
            for (i = 0; i < DUKEVR_OPENXR_MOVABLE_LAYERS; ++i) {
                if (!gOpenXR.movable_content_submitted[i] ||
                    !gOpenXR.movable_swapchains_acquired[i])
                    continue;
                movable_layers[i] = hud_layer;
                movable_layers[i].subImage.swapchain = gOpenXR.movable_swapchains[i];
                movable_layers[i].pose.position.x =
                    ((float)gOpenXR.movable_offsets[i][0] / 320.0f) * hud_layer.size.width;
                movable_layers[i].pose.position.y =
                    -((float)gOpenXR.movable_offsets[i][1] / 200.0f) * hud_layer.size.height;
                layers[layer_count++] =
                    (const XrCompositionLayerBaseHeader*)&movable_layers[i];
            }
            {
                static int loggedMovableLayers;
                if (!loggedMovableLayers &&
                    (gOpenXR.movable_content_submitted[0] ||
                     gOpenXR.movable_content_submitted[1])) {
                    LOG_F(INFO, "OpenXR independent HUD layers submitted: weapon=%d status=%d offsets=%d/%d,%d/%d",
                        gOpenXR.movable_content_submitted[0],
                        gOpenXR.movable_content_submitted[1],
                        gOpenXR.movable_offsets[0][0], gOpenXR.movable_offsets[0][1],
                        gOpenXR.movable_offsets[1][0], gOpenXR.movable_offsets[1][1]);
                    loggedMovableLayers = 1;
                }
            }
            layers[layer_count++] = (const XrCompositionLayerBaseHeader*)&hud_layer;
        }
    }
    else
        layers[layer_count++] = (const XrCompositionLayerBaseHeader*)&projection_layer;
    {
        static int traceCount;
        static int loggedMenuScale;
        if (gOpenXR.hud_content_submitted &&
            (traceCount < 8 || (gOpenXR.hud_menu_scaled && !loggedMenuScale))) {
            LOG_F(INFO, "OpenXR HUD composition layer submitted: menuScale=%d size=%0.3fx%0.3f",
                gOpenXR.hud_menu_scaled, hud_layer.size.width, hud_layer.size.height);
            if (traceCount < 8)
                traceCount++;
            if (gOpenXR.hud_menu_scaled)
                loggedMenuScale = 1;
        }
    }
    memset(&end_info, 0, sizeof(end_info));
    end_info.type = XR_TYPE_FRAME_END_INFO;
    end_info.displayTime = gOpenXR.predicted_display_time;
    end_info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    end_info.layerCount = layer_count;
    end_info.layers = layers;

    glFlush();
    for (i = 0; i < 2; i++) {
        XrSwapchainImageReleaseInfo release_info;
        memset(&release_info, 0, sizeof(release_info));
        release_info.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
        if (gOpenXR.swapchain_acquired[i])
            xrReleaseSwapchainImage(gOpenXR.swapchains[i], &release_info);
        gOpenXR.swapchain_acquired[i] = 0;
        gOpenXR.images_acquired[i] = 0;
    }
    {
        XrSwapchainImageReleaseInfo release_info;
        memset(&release_info, 0, sizeof(release_info));
        release_info.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
        if (gOpenXR.hud_swapchain_acquired)
            xrReleaseSwapchainImage(gOpenXR.hud_swapchain, &release_info);
        gOpenXR.hud_swapchain_acquired = 0;
        gOpenXR.hud_image_acquired = 0;
        for (i = 0; i < DUKEVR_OPENXR_MOVABLE_LAYERS; ++i) {
            if (gOpenXR.movable_swapchains_acquired[i])
                xrReleaseSwapchainImage(gOpenXR.movable_swapchains[i], &release_info);
            gOpenXR.movable_swapchains_acquired[i] = 0;
            gOpenXR.movable_images_acquired[i] = 0;
            gOpenXR.movable_content_submitted[i] = 0;
        }
    }
    OpenXRResultOK(xrEndFrame(gOpenXR.session, &end_info), "xrEndFrame");
    gOpenXR.frame_active = 0;
    gOpenXR.frame_begun = 0;
    /* This is per-frame state. Leaving it set makes the next mono/menu
     * presentation look like an in-progress stereo scene to winlayer. */
    gOpenXR.scene_frame_submitted = 0;
    gOpenXR.mono_frame_submitted = 0;
    gOpenXR.mono_quad_submitted = 0;
    gOpenXR.mono_source_width = 0;
    gOpenXR.mono_source_height = 0;
    gOpenXR.hud_menu_scaled = 0;
    gOpenXR.current_eye = -1;
}

void DukeVROpenXR_Shutdown(void) {
    initprintf("OpenXR: shutdown requested");
    if (gOpenXR.frame_active || gOpenXR.frame_begun)
        DukeVROpenXR_EndFrame();
    OpenXRReleaseResources();
    initprintf("OpenXR: shutdown complete");
}

/* LibOVR-shaped adapters used by the original DukeVR renderer. */
static int DukeVROpenXR_EyeForChain(ovrTextureSwapChain chain) {
    DukeVROVRSwapChain *c = (DukeVROVRSwapChain *)chain;
    return c == NULL ? -1 : c->eye;
}

ovrResult ovr_Initialize(const void *params) {
    (void)params;
    return ovrSuccess;
}

void ovr_Shutdown(void) {
    DukeVROpenXR_Shutdown();
}

ovrResult ovr_Create(ovrSession *session, ovrGraphicsLuid *luid) {
    (void)luid;
    if (session == NULL || !DukeVROpenXR_Initialize())
        return -1;
    *session = (ovrSession)&gOpenXR;
    return ovrSuccess;
}

void ovr_Destroy(ovrSession session) {
    (void)session;
    DukeVROpenXR_Shutdown();
}

ovrHmdDesc ovr_GetHmdDesc(ovrSession session) {
    ovrHmdDesc desc;
    int width = 1024, height = 1024;
    (void)session;
    memset(&desc, 0, sizeof(desc));
    desc.Type = ovrHmd_CV1;
    DukeVROpenXR_GetEyeDimensions(0, &width, &height);
#ifdef OPENXR
    /* EDuke32 uses HmdDesc.Resolution as the actual OpenGL render target
     * size. The old Oculus path supplied a combined side-by-side width, but
     * OpenXR gives us one swapchain image per eye. Keep the renderer and each
     * submitted image at the same per-eye dimensions. */
    desc.Resolution.w = width;
#else
    desc.Resolution.w = width * 2;
#endif
    desc.Resolution.h = height;
    desc.DefaultEyeFov[0].LeftTan = desc.DefaultEyeFov[0].RightTan = 1.0f;
    desc.DefaultEyeFov[0].UpTan = desc.DefaultEyeFov[0].DownTan = 1.0f;
    desc.DefaultEyeFov[1] = desc.DefaultEyeFov[0];
    return desc;
}

ovrEyeRenderDesc ovr_GetRenderDesc(ovrSession session, ovrEyeType eye, ovrFovPort fov) {
    ovrEyeRenderDesc desc;
    (void)session;
    memset(&desc, 0, sizeof(desc));
    desc.HmdToEyeOffset.x = eye == ovrEye_Left ? gOpenXR.eye_offset_x[0] : gOpenXR.eye_offset_x[1];
    desc.Fov = fov;
    /* Once xrLocateViews has run, use the runtime's asymmetric FOV as well.
     * The old Oculus defaults are only a bootstrap value before the first
     * frame. */
    if (gOpenXR.frame_active) {
        int index = eye == ovrEye_Left ? 0 : 1;
        desc.Fov.LeftTan = tanf(-gOpenXR.views[index].fov.angleLeft);
        desc.Fov.RightTan = tanf(gOpenXR.views[index].fov.angleRight);
        desc.Fov.UpTan = tanf(gOpenXR.views[index].fov.angleUp);
        desc.Fov.DownTan = tanf(-gOpenXR.views[index].fov.angleDown);
    }
    return desc;
}

ovrResult ovr_GetEyePoses(ovrSession session, uint64_t frameIndex, ovrBool latencyMarker,
    const ovrVector3f hmdToEyeOffset[2], ovrPosef outEyePoses[2], double *timing) {
    float orientation[4], position[3];
    int eye;
    (void)session; (void)frameIndex; (void)latencyMarker; (void)hmdToEyeOffset;
    if (!DukeVROpenXR_BeginFrame())
        return -1;
    if (timing != NULL)
        *timing = (double)gOpenXR.predicted_display_time * 1e-9;
    for (eye = 0; eye < 2; eye++) {
        if (!DukeVROpenXR_GetEyePose(eye, orientation, position))
            return -1;
        outEyePoses[eye].Orientation.x = orientation[0];
        outEyePoses[eye].Orientation.y = orientation[1];
        outEyePoses[eye].Orientation.z = orientation[2];
        outEyePoses[eye].Orientation.w = orientation[3];
        outEyePoses[eye].Position.x = position[0];
        outEyePoses[eye].Position.y = position[1];
        outEyePoses[eye].Position.z = position[2];
    }
    return ovrSuccess;
}

ovrResult ovr_CalcEyePoses(ovrPosef headPose, const ovrVector3f hmdToEyeOffset[2], ovrPosef outEyePoses[2]) {
    int eye;
    for (eye = 0; eye < 2; eye++) {
        outEyePoses[eye] = headPose;
        outEyePoses[eye].Position.x += hmdToEyeOffset[eye].x;
        outEyePoses[eye].Position.y += hmdToEyeOffset[eye].y;
        outEyePoses[eye].Position.z += hmdToEyeOffset[eye].z;
    }
    return ovrSuccess;
}

double ovr_GetPredictedDisplayTime(ovrSession session, uint64_t frameIndex) {
    (void)session; (void)frameIndex;
    return (double)gOpenXR.predicted_display_time * 1e-9;
}

ovrTrackingState ovr_GetTrackingState(ovrSession session, double absTime, ovrBool latencyMarker) {
    ovrTrackingState state;
    float orientation[4], position[3], right_orientation[4], right_position[3];
    (void)session; (void)absTime; (void)latencyMarker;
    memset(&state, 0, sizeof(state));
    /* The original LibOVR call order asks for tracking before the later
     * GetEyePoses call. Make that order valid for OpenXR so head pose data is
     * not silently reported as zero on every normal frame. */
    if (!gOpenXR.frame_active && !DukeVROpenXR_BeginFrame())
        return state;
    /* OpenXR view poses are eye poses. The legacy renderer applies the eye
     * offsets itself, so expose the midpoint as the head pose or the IPD is
     * applied twice (and the two eyes no longer share a common center). */
    if (DukeVROpenXR_GetEyePose(0, orientation, position) &&
        DukeVROpenXR_GetEyePose(1, right_orientation, right_position)) {
        position[0] = (position[0] + right_position[0]) * 0.5f;
        position[1] = (position[1] + right_position[1]) * 0.5f;
        position[2] = (position[2] + right_position[2]) * 0.5f;
        state.HeadPose.ThePose.Orientation.x = orientation[0];
        state.HeadPose.ThePose.Orientation.y = orientation[1];
        state.HeadPose.ThePose.Orientation.z = orientation[2];
        state.HeadPose.ThePose.Orientation.w = orientation[3];
        state.HeadPose.ThePose.Position.x = position[0];
        state.HeadPose.ThePose.Position.y = position[1];
        state.HeadPose.ThePose.Position.z = position[2];
        state.StatusFlags = ovrStatus_PositionTracked;
    }
    return state;
}

ovrResult ovr_RecenterTrackingOrigin(ovrSession session) {
    (void)session;
    return ovrSuccess;
}

ovrResult ovr_CreateTextureSwapChainGL(ovrSession session, const ovrTextureSwapChainDesc *desc,
    ovrTextureSwapChain *outChain) {
    DukeVROVRSwapChain *chain;
    int width = desc != NULL ? desc->Width : 1024;
    int height = desc != NULL ? desc->Height : 1024;
    (void)session;
    if (outChain == NULL)
        return -1;
    if (!DukeVROpenXR_InitializeGraphics())
        return -1;
    chain = (DukeVROVRSwapChain *)calloc(1, sizeof(*chain));
    if (chain == NULL)
        return -1;
    if (gDukeVROpenXREyeChainCount < 2 &&
        gOpenXR.swapchains[gDukeVROpenXREyeChainCount] != XR_NULL_HANDLE) {
        chain->eye = gDukeVROpenXREyeChainCount++;
    } else if (gDukeVROpenXREyeChainCount == 2 &&
        gOpenXR.hud_swapchain != XR_NULL_HANDLE) {
        chain->eye = -1;
        chain->auxiliary = 1;
        chain->hud = 1;
        gDukeVROpenXREyeChainCount++;
    } else {
        chain->eye = -1;
        chain->auxiliary = 1;
        glGenTextures(1, &chain->texture);
        glBindTexture(GL_TEXTURE_2D, chain->texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }
    *outChain = chain;
    return ovrSuccess;
}

ovrResult ovr_DestroyTextureSwapChain(ovrSession session, ovrTextureSwapChain chain) {
    DukeVROVRSwapChain *c = (DukeVROVRSwapChain *)chain;
    (void)session;
    if (c == NULL)
        return ovrSuccess;
    if (c->auxiliary && c->texture != 0)
        glDeleteTextures(1, &c->texture);
    free(c);
    return ovrSuccess;
}

ovrResult ovr_GetTextureSwapChainLength(ovrSession session, ovrTextureSwapChain chain, int *length) {
    DukeVROVRSwapChain *c = (DukeVROVRSwapChain *)chain;
    int eye = DukeVROpenXR_EyeForChain(chain);
    (void)session;
    if (c == NULL || length == NULL)
        return -1;
    *length = eye >= 0 ? (int)gOpenXR.image_counts[eye] :
        (c->hud ? (int)gOpenXR.hud_image_count : 1);
    return ovrSuccess;
}

ovrResult ovr_GetTextureSwapChainCurrentIndex(ovrSession session, ovrTextureSwapChain chain, int *index) {
    DukeVROVRSwapChain *c = (DukeVROVRSwapChain *)chain;
    int eye = DukeVROpenXR_EyeForChain(chain);
    (void)session;
    if (c == NULL || index == NULL)
        return -1;
    if (c->hud)
        *index = gOpenXR.frame_active ? (int)gOpenXR.hud_image_acquired : 0;
    else
        *index = eye >= 0 && gOpenXR.frame_active ? (int)gOpenXR.images_acquired[eye] : 0;
    return ovrSuccess;
}

ovrResult ovr_GetTextureSwapChainBufferGL(ovrSession session, ovrTextureSwapChain chain, int index, unsigned int *texId) {
    DukeVROVRSwapChain *c = (DukeVROVRSwapChain *)chain;
    (void)session; (void)index;
    if (c == NULL || texId == NULL)
        return -1;
    *texId = c->hud ? DukeVROpenXR_GetHudTexture() :
        (c->eye >= 0 ? DukeVROpenXR_GetEyeTexture(c->eye) : c->texture);
    return *texId != 0 ? ovrSuccess : -1;
}

ovrResult ovr_CommitTextureSwapChain(ovrSession session, ovrTextureSwapChain chain) {
    (void)session; (void)chain;
    return ovrSuccess;
}

ovrResult ovr_CreateMirrorTextureGL(ovrSession session, const ovrMirrorTextureDesc *desc, ovrMirrorTexture *outMirror) {
    ovrTextureSwapChain chain = NULL;
    ovrTextureSwapChainDesc chain_desc;
    (void)session;
    if (outMirror == NULL)
        return -1;
    memset(&chain_desc, 0, sizeof(chain_desc));
    chain_desc.Width = desc != NULL ? desc->Width : 1280;
    chain_desc.Height = desc != NULL ? desc->Height : 720;
    if (ovr_CreateTextureSwapChainGL(session, &chain_desc, &chain) < 0)
        return -1;
    {
        DukeVROVRSwapChain *mirror_chain = (DukeVROVRSwapChain *)chain;
        mirror_chain->eye = -1;
        mirror_chain->auxiliary = 1;
        mirror_chain->hud = 0;
        if (mirror_chain->texture == 0) {
            glGenTextures(1, &mirror_chain->texture);
            glBindTexture(GL_TEXTURE_2D, mirror_chain->texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, chain_desc.Width, chain_desc.Height,
                0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        }
    }
    *outMirror = (ovrMirrorTexture)chain;
    return ovrSuccess;
}

ovrResult ovr_GetMirrorTextureBufferGL(ovrSession session, ovrMirrorTexture mirror, unsigned int *texId) {
    return ovr_GetTextureSwapChainBufferGL(session, (ovrTextureSwapChain)mirror, 0, texId);
}

ovrResult ovr_DestroyMirrorTexture(ovrSession session, ovrMirrorTexture mirror) {
    return ovr_DestroyTextureSwapChain(session, (ovrTextureSwapChain)mirror);
}

ovrResult ovr_SubmitFrame(ovrSession session, uint64_t frameIndex, const ovrViewScaleDesc *viewScale,
    ovrLayerHeader const * const *layerPtrList, unsigned int layerCount) {
    (void)session; (void)frameIndex; (void)viewScale; (void)layerPtrList; (void)layerCount;
    DukeVROpenXR_EndFrame();
    return ovrSuccess;
}

unsigned int ovr_GetConnectedControllerTypes(ovrSession session) {
    (void)session;
    return 0;
}

ovrResult ovr_GetInputState(ovrSession session, unsigned int controllerType, ovrInputState *inputState) {
    (void)session; (void)controllerType;
    if (inputState != NULL)
        memset(inputState, 0, sizeof(*inputState));
    return -1;
}

ovrResult ovr_SetControllerVibration(ovrSession session, unsigned int controllerType, float frequency, float amplitude) {
    (void)session; (void)controllerType; (void)frequency; (void)amplitude;
    return ovrSuccess;
}

#else

int DukeVROpenXR_Enabled(void) { return 0; }
int DukeVROpenXR_Initialize(void) { return 0; }
int DukeVROpenXR_PrepareInput(void) { return 0; }
int DukeVROpenXR_BeginFrame(void) { return 0; }
int DukeVROpenXR_FrameActive(void) { return 0; }
int DukeVROpenXR_BeginEyeRender(int eye) { (void)eye; return 0; }
void DukeVROpenXR_EndEyeRender(void) {}
int DukeVROpenXR_BeginHudRender(void) { return 0; }
void DukeVROpenXR_EndHudRender(void) {}
int DukeVROpenXR_BeginHudLayer(int layer) { (void)layer; return 0; }
void DukeVROpenXR_EndHudLayer(void) {}
void DukeVROpenXR_SetHudLayerOffsets(int weapon_x, int weapon_y,
    int status_x, int status_y) {
    (void)weapon_x; (void)weapon_y; (void)status_x; (void)status_y;
}
void DukeVROpenXR_SetHudMenuScale(int scaled) { (void)scaled; }
int DukeVROpenXR_CurrentEye(void) { return -1; }
int DukeVROpenXR_SceneFrameActive(void) { return 0; }
int DukeVROpenXR_PresentMirror(int eye, int width, int height) {
    (void)eye; (void)width; (void)height; return 0;
}
void DukeVROpenXR_MarkSceneFrame(void) {}
int DukeVROpenXR_ConsumeSceneFrame(void) { return 0; }
void DukeVROpenXR_MarkMonoFrame(void) {}
int DukeVROpenXR_GetEyePose(int eye, float orientation_xyzw[4], float position_xyz[3]) {
    (void)eye;
    (void)orientation_xyzw;
    (void)position_xyz;
    return 0;
}
int DukeVROpenXR_GetControllerPose(int hand, float position_xyz[3],
    float orientation_xyzw[4], int* grip_pressed) {
    (void)hand; (void)position_xyz; (void)orientation_xyzw; (void)grip_pressed;
    return 0;
}
int DukeVROpenXR_GetControllerInput(DukeVROpenXRControllerInput* input) {
    (void)input;
    return 0;
}
void DukeVROpenXR_ApplyGameplayInput(void* control_info) { (void)control_info; }
int DukeVROpenXR_GetMenuInput(int* direction, int* advance, int* back, int* escape) {
    if (direction != NULL) *direction = 0;
    if (advance != NULL) *advance = 0;
    if (back != NULL) *back = 0;
    if (escape != NULL) *escape = 0;
    return 0;
}
void DukeVROpenXR_ClearMenuDirection(void) {}
void DukeVROpenXR_ClearMenuAdvance(void) {}
void DukeVROpenXR_ClearMenuBack(void) {}
void DukeVROpenXR_ClearMenuEscape(void) {}
void DukeVROpenXR_ClearMenuInput(void) {}
int DukeVROpenXR_MenuStickActive(void) { return 0; }
int DukeVROpenXR_ConsumeSnapTurn(void) { return 0; }
int DukeVROpenXR_ConsumeWeaponChange(void) { return 0; }
int DukeVROpenXR_GetEyeFov(int eye, float* angle_left, float* angle_right,
    float* angle_up, float* angle_down) {
    (void)eye; (void)angle_left; (void)angle_right; (void)angle_up; (void)angle_down;
    return 0;
}
void DukeVROpenXR_SetRenderOrientationResidual(float yaw_radians,
    float pitch_radians, float roll_radians) {
    (void)yaw_radians; (void)pitch_radians; (void)roll_radians;
}
void DukeVROpenXR_GetRenderOrientationResidual(float* yaw_radians,
    float* pitch_radians, float* roll_radians) {
    if (yaw_radians != NULL) *yaw_radians = 0.0f;
    if (pitch_radians != NULL) *pitch_radians = 0.0f;
    if (roll_radians != NULL) *roll_radians = 0.0f;
}
void DukeVROpenXR_SetRenderPositionResidual(float x_build, float y_build,
    float z_build) {
    (void)x_build; (void)y_build; (void)z_build;
}
void DukeVROpenXR_GetRenderPositionResidual(float* x_build, float* y_build,
    float* z_build) {
    if (x_build != NULL) *x_build = 0.0f;
    if (y_build != NULL) *y_build = 0.0f;
    if (z_build != NULL) *z_build = 0.0f;
}
int DukeVROpenXR_GetEyeDimensions(int eye, int* width, int* height) {
    (void)eye; (void)width; (void)height;
    return 0;
}
int DukeVROpenXR_GetSceneDimensions(int* width, int* height) {
    (void)width; (void)height;
    return 0;
}
uint32_t DukeVROpenXR_GetEyeTexture(int eye) { (void)eye; return 0; }
uint32_t DukeVROpenXR_GetHudTexture(void) { return 0; }
void DukeVROpenXR_MarkHudFrame(void) {}
int DukeVROpenXR_SubmitTexture(void* source) { (void)source; return 0; }
int DukeVROpenXR_SubmitDesktopFrame(int width, int height) {
    (void)width; (void)height; return 0;
}
void DukeVROpenXR_EndFrame(void) {}
void DukeVROpenXR_Shutdown(void) {}

#endif
